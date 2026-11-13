// SPDX-License-Identifier: GPL-2.0-only
#include "assembler.h"
#include "assembler_resources.h"
#include "opcode_metadata.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace emu51_asm {

bool AssemblyImage::empty() const {
    return bytes.empty();
}

std::uint16_t AssemblyImage::lowest_address() const {
    return bytes.empty() ? 0 : bytes.begin()->first;
}

std::uint16_t AssemblyImage::highest_address() const {
    return bytes.empty() ? 0 : bytes.rbegin()->first;
}

std::vector<std::uint8_t> AssemblyImage::to_binary() const {
    if (bytes.empty()) {
        return {};
    }

    const std::uint16_t start = lowest_address();
    const std::uint16_t end = highest_address();
    std::vector<std::uint8_t> binary(static_cast<std::size_t>(end - start) + 1u, 0);
    for (const auto& [address, value] : bytes) {
        binary[static_cast<std::size_t>(address - start)] = value;
    }
    return binary;
}

std::string AssemblyImage::to_intel_hex(std::size_t record_size) const {
    if (record_size == 0) {
        record_size = 16;
    }

    auto append_record = [](std::ostringstream& out,
                            std::uint8_t count,
                            std::uint16_t address,
                            std::uint8_t type,
                            const std::vector<std::uint8_t>& data) {
        unsigned sum = count + static_cast<unsigned>(address >> 8) +
                       static_cast<unsigned>(address & 0xff) + type;
        out << ':';
        auto hex_byte = [&out](std::uint8_t value) {
            static const char* digits = "0123456789ABCDEF";
            out << digits[(value >> 4) & 0x0f] << digits[value & 0x0f];
        };
        hex_byte(count);
        hex_byte(static_cast<std::uint8_t>(address >> 8));
        hex_byte(static_cast<std::uint8_t>(address & 0xff));
        hex_byte(type);
        for (std::uint8_t value : data) {
            hex_byte(value);
            sum += value;
        }
        const std::uint8_t checksum = static_cast<std::uint8_t>((0x100u - (sum & 0xffu)) & 0xffu);
        hex_byte(checksum);
        out << '\n';
    };

    std::ostringstream out;
    auto it = bytes.begin();
    while (it != bytes.end()) {
        const std::uint16_t start = it->first;
        std::vector<std::uint8_t> chunk;
        chunk.reserve(record_size);
        std::uint16_t expected = start;
        while (it != bytes.end() && it->first == expected && chunk.size() < record_size) {
            chunk.push_back(it->second);
            ++it;
            ++expected;
        }
        append_record(out, static_cast<std::uint8_t>(chunk.size()), start, 0x00, chunk);
    }

    append_record(out, 0x00, 0x0000, 0x01, {});
    return out.str();
}

bool AssembleResult::success() const {
    return diagnostics.empty();
}

namespace {

namespace fs = std::filesystem;

std::optional<std::string> read_source_stream(std::istream& input) {
    std::string source;
    char chunk[4096];
    while (input.read(chunk, sizeof(chunk)) || input.gcount() != 0)
        source.append(chunk, static_cast<std::size_t>(input.gcount()));
    if (input.bad() || !input.eof())
        return std::nullopt;
    return source;
}

struct SourceLocation {
    std::string file;
    std::size_t line = 0;
};

struct SymbolInfo {
    Symbol public_symbol;
    SourceLocation defined_at;
};

enum class StatementKind {
    label_only,
    definition,
    org,
    db,
    dw,
    instruction,
    end_directive,
};

struct Statement {
    SourceLocation location;
    std::optional<std::string> label;
    StatementKind kind = StatementKind::instruction;
    std::string keyword;
    std::vector<std::string> arguments;
    SymbolKind definition_kind = SymbolKind::equ;
};

enum class ParsedOperandKind {
    expression,
    immediate,
    accumulator,
    carry,
    ab,
    dptr,
    at_dptr,
    reg,
    indirect_reg,
    at_a_plus_dptr,
    at_a_plus_pc,
    negated_bit,
};

struct ParsedOperand {
    ParsedOperandKind kind = ParsedOperandKind::expression;
    std::string text;
    int reg_index = -1;
    std::optional<std::string> simple_symbol;
};

enum class OperandPatternKind {
    accumulator,
    carry,
    ab,
    dptr,
    at_dptr,
    reg,
    indirect_reg,
    at_a_plus_dptr,
    at_a_plus_pc,
    direct8,
    immediate8,
    immediate16,
    bit8,
    negated_bit8,
    relative8,
    address16,
    address11,
};

struct OperandPattern {
    OperandPatternKind kind = OperandPatternKind::direct8;
    int reg_index = -1;
    int byte_index = -1;
    int high_byte_index = -1;
    int low_byte_index = -1;
    int addr11_page = -1;
};

std::string trim_copy(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

std::string to_upper_copy(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return out;
}

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '.' || ch == '$' || ch == '?';
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '.' || ch == '$' || ch == '?';
}

bool is_identifier(std::string_view text) {
    if (text.empty() || !is_identifier_start(text.front())) {
        return false;
    }
    return std::all_of(text.begin() + 1, text.end(), [](char ch) { return is_identifier_char(ch); });
}

std::string strip_comment(std::string_view text) {
    const std::size_t pos = text.find(';');
    return trim_copy(pos == std::string_view::npos ? text : text.substr(0, pos));
}

std::vector<std::string> split_top_level(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            depth = std::max(0, depth - 1);
        } else if (ch == delimiter && depth == 0) {
            parts.push_back(trim_copy(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim_copy(text.substr(start)));
    return parts;
}

std::optional<std::uint64_t> parse_number_token(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }

    int base = 10;
    std::string digits = token;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        base = 16;
        digits = digits.substr(2);
    } else {
        const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(digits.back())));
        if (suffix == 'H') {
            base = 16;
            digits.pop_back();
        } else if (suffix == 'B') {
            base = 2;
            digits.pop_back();
        }
    }

    if (digits.empty()) {
        return std::nullopt;
    }

    auto valid_digit = [base](char ch) {
        if (base == 2) {
            return ch == '0' || ch == '1';
        }
        if (base == 10) {
            return std::isdigit(static_cast<unsigned char>(ch)) != 0;
        }
        return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
    };

    if (!std::all_of(digits.begin(), digits.end(), valid_digit)) {
        return std::nullopt;
    }

    try {
        return std::stoull(digits, nullptr, base);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> simple_identifier_expression(std::string_view expr) {
    const std::string trimmed = trim_copy(expr);
    if (is_identifier(trimmed)) {
        return to_upper_copy(trimmed);
    }
    return std::nullopt;
}

class ExpressionEvaluator {
public:
    struct Result {
        bool ok = false;
        bool unresolved = false;
        std::int64_t value = 0;
        std::string error;
    };

    ExpressionEvaluator(std::string_view text,
                        const std::map<std::string, SymbolInfo>& symbols,
                        bool allow_unresolved)
        : text_(text), symbols_(symbols), allow_unresolved_(allow_unresolved) {}

    Result evaluate() {
        skip_spaces();
        Result result = parse_expression();
        if (!result.ok) {
            return result;
        }
        skip_spaces();
        if (pos_ != text_.size()) {
            return {false, false, 0, "unexpected token in expression"};
        }
        result.ok = true;
        return result;
    }

private:
    Result parse_expression(unsigned depth = 0) {
        Result left = parse_factor(depth);
        if (!left.ok) {
            return left;
        }

        while (true) {
            skip_spaces();
            if (pos_ >= text_.size()) {
                return left;
            }

            const char op = text_[pos_];
            if (op != '+' && op != '-') {
                return left;
            }
            ++pos_;

            Result right = parse_factor(depth);
            if (!right.ok) {
                return right;
            }

            left.unresolved = left.unresolved || right.unresolved;
            if (!left.unresolved) {
                constexpr auto min = std::numeric_limits<std::int64_t>::min();
                constexpr auto max = std::numeric_limits<std::int64_t>::max();
                const bool overflow = op == '+'
                    ? ((right.value > 0 && left.value > max - right.value) ||
                       (right.value < 0 && left.value < min - right.value))
                    : ((right.value > 0 && left.value < min + right.value) ||
                       (right.value < 0 && left.value > max + right.value));
                if (overflow)
                    return {false, false, 0, "integer overflow in expression"};
                left.value = (op == '+') ? (left.value + right.value) : (left.value - right.value);
            }
        }
    }

    Result parse_factor(unsigned depth) {
        if (depth >= 128)
            return {false, false, 0, "expression nesting limit exceeded"};
        skip_spaces();
        if (pos_ >= text_.size()) {
            return {false, false, 0, "expected expression"};
        }

        const char ch = text_[pos_];
        if (ch == '+') {
            ++pos_;
            return parse_factor(depth + 1);
        }
        if (ch == '-') {
            ++pos_;
            Result inner = parse_factor(depth + 1);
            if (!inner.ok) {
                return inner;
            }
            if (!inner.unresolved) {
                if (inner.value == std::numeric_limits<std::int64_t>::min())
                    return {false, false, 0, "integer overflow in negation"};
                inner.value = -inner.value;
            }
            return inner;
        }
        if (ch == '(') {
            ++pos_;
            Result inner = parse_expression(depth + 1);
            if (!inner.ok) {
                return inner;
            }
            skip_spaces();
            if (pos_ >= text_.size() || text_[pos_] != ')') {
                return {false, false, 0, "missing ')' in expression"};
            }
            ++pos_;
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            const std::size_t start = pos_;
            while (pos_ < text_.size() &&
                   (std::isalnum(static_cast<unsigned char>(text_[pos_])) != 0 || text_[pos_] == 'x' ||
                    text_[pos_] == 'X')) {
                ++pos_;
            }
            const auto number = parse_number_token(std::string(text_.substr(start, pos_ - start)));
            if (!number.has_value()) {
                return {false, false, 0, "invalid numeric literal"};
            }
            if (*number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                return {false, false, 0, "numeric literal exceeds signed 64-bit range"};
            return {true, false, static_cast<std::int64_t>(*number), {}};
        }
        if (is_identifier_start(ch)) {
            const std::size_t start = pos_;
            ++pos_;
            while (pos_ < text_.size() && is_identifier_char(text_[pos_])) {
                ++pos_;
            }
            const std::string name = to_upper_copy(text_.substr(start, pos_ - start));
            const auto it = symbols_.find(name);
            if (it == symbols_.end()) {
                if (allow_unresolved_) {
                    return {true, true, 0, {}};
                }
                return {false, false, 0, "unknown symbol '" + std::string(text_.substr(start, pos_ - start)) + "'"};
            }
            return {true, false, static_cast<std::int64_t>(it->second.public_symbol.value), {}};
        }

        return {false, false, 0, "expected expression"};
    }

    void skip_spaces() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
            ++pos_;
        }
    }

    std::string_view text_;
    const std::map<std::string, SymbolInfo>& symbols_;
    bool allow_unresolved_ = false;
    std::size_t pos_ = 0;
};

std::optional<std::int64_t> checked_value(const ExpressionEvaluator::Result& eval,
                                          std::int64_t min_value,
                                          std::int64_t max_value,
                                          std::string& error) {
    if (!eval.ok) {
        error = eval.error;
        return std::nullopt;
    }
    if (eval.unresolved) {
        return std::nullopt;
    }
    if (eval.value < min_value || eval.value > max_value) {
        error = "value out of range";
        return std::nullopt;
    }
    return eval.value;
}

ParsedOperand parse_operand(const std::string& operand_text) {
    ParsedOperand operand;
    operand.text = trim_copy(operand_text);
    const std::string upper = to_upper_copy(operand.text);

    if (upper == "A") {
        operand.kind = ParsedOperandKind::accumulator;
        return operand;
    }
    if (upper == "C") {
        operand.kind = ParsedOperandKind::carry;
        return operand;
    }
    if (upper == "AB") {
        operand.kind = ParsedOperandKind::ab;
        return operand;
    }
    if (upper == "DPTR") {
        operand.kind = ParsedOperandKind::dptr;
        return operand;
    }
    if (upper == "@DPTR") {
        operand.kind = ParsedOperandKind::at_dptr;
        return operand;
    }
    if (upper == "@A+DPTR") {
        operand.kind = ParsedOperandKind::at_a_plus_dptr;
        return operand;
    }
    if (upper == "@A+PC") {
        operand.kind = ParsedOperandKind::at_a_plus_pc;
        return operand;
    }
    if (upper == "@R0" || upper == "@R1") {
        operand.kind = ParsedOperandKind::indirect_reg;
        operand.reg_index = upper.back() - '0';
        return operand;
    }
    if (upper.size() == 2 && upper[0] == 'R' && upper[1] >= '0' && upper[1] <= '7') {
        operand.kind = ParsedOperandKind::reg;
        operand.reg_index = upper[1] - '0';
        return operand;
    }
    if (!operand.text.empty() && operand.text.front() == '#') {
        operand.kind = ParsedOperandKind::immediate;
        operand.text = trim_copy(std::string_view(operand.text).substr(1));
        operand.simple_symbol = simple_identifier_expression(operand.text);
        return operand;
    }
    if (!operand.text.empty() && operand.text.front() == '/') {
        operand.kind = ParsedOperandKind::negated_bit;
        operand.text = trim_copy(std::string_view(operand.text).substr(1));
        operand.simple_symbol = simple_identifier_expression(operand.text);
        return operand;
    }

    operand.kind = ParsedOperandKind::expression;
    operand.simple_symbol = simple_identifier_expression(operand.text);
    return operand;
}

} // namespace

class OpcodeTable {
public:
    struct Descriptor {
        std::uint8_t opcode = 0;
        std::string mnemonic;
        std::string pattern_text;
        std::uint8_t length = 0;
        std::vector<OperandPattern> operands;
    };

    static const OpcodeTable& builtin() {
        static const OpcodeTable table = [] {
            OpcodeTable result;
            result.descriptors_.reserve(255);
            std::array<const code_51*, 256> ordered{};
            for (const auto& definition : asm51) {
                ordered[definition.assemblyOrder] = &definition;
            }
            for (std::size_t i = 0; i < 255; ++i) {
                const auto& definition = *ordered[i];
                Descriptor descriptor;
                descriptor.opcode = definition.code;
                descriptor.mnemonic = to_upper_copy(definition.mnem);
                descriptor.pattern_text = definition.datas;
                descriptor.length = definition.lenght;
                descriptor.operands = parse_pattern(descriptor.pattern_text);
                result.descriptors_.push_back(std::move(descriptor));
            }
            return result;
        }();
        return table;
    }

    std::vector<const Descriptor*> lookup(std::string_view mnemonic) const {
        std::vector<const Descriptor*> matches;
        const std::string upper = to_upper_copy(mnemonic);
        for (const Descriptor& descriptor : descriptors_) {
            if (descriptor.mnemonic == upper) {
                matches.push_back(&descriptor);
            }
        }
        return matches;
    }

    std::vector<std::string> mnemonics() const {
        std::vector<std::string> names;
        for (const Descriptor& descriptor : descriptors_) {
            if (std::find(names.begin(), names.end(), descriptor.mnemonic) == names.end()) {
                names.push_back(descriptor.mnemonic);
            }
        }
        return names;
    }

private:
    static std::vector<OperandPattern> parse_pattern(const std::string& pattern_text) {
        if (pattern_text == "%" || pattern_text.empty()) {
            return {};
        }

        std::vector<OperandPattern> patterns;
        for (const std::string& token : split_top_level(pattern_text, ',')) {
            const std::string upper = to_upper_copy(token);
            OperandPattern pattern;
            const auto placeholder_digit = [](char ch) -> int {
                return std::isdigit(static_cast<unsigned char>(ch)) != 0 ? ch - '0' : -1;
            };

            if (upper == "A") {
                pattern.kind = OperandPatternKind::accumulator;
            } else if (upper == "C") {
                pattern.kind = OperandPatternKind::carry;
            } else if (upper == "AB") {
                pattern.kind = OperandPatternKind::ab;
            } else if (upper == "DPTR") {
                pattern.kind = OperandPatternKind::dptr;
            } else if (upper == "@DPTR") {
                pattern.kind = OperandPatternKind::at_dptr;
            } else if (upper == "@A+DPTR") {
                pattern.kind = OperandPatternKind::at_a_plus_dptr;
            } else if (upper == "@A+PC") {
                pattern.kind = OperandPatternKind::at_a_plus_pc;
            } else if (upper == "@R0" || upper == "@R1") {
                pattern.kind = OperandPatternKind::indirect_reg;
                pattern.reg_index = upper.back() - '0';
            } else if (upper.size() == 2 && upper[0] == 'R' && upper[1] >= '0' && upper[1] <= '7') {
                pattern.kind = OperandPatternKind::reg;
                pattern.reg_index = upper[1] - '0';
            } else if (upper.size() == 3 && upper[0] == '#' && upper[1] == '%' &&
                       placeholder_digit(upper[2]) > 0) {
                pattern.kind = OperandPatternKind::immediate8;
                pattern.byte_index = placeholder_digit(upper[2]);
            } else if (upper.size() == 5 && upper[0] == '#' && upper[1] == '%' &&
                       placeholder_digit(upper[2]) > 0 && upper[3] == '%' &&
                       placeholder_digit(upper[4]) > 0) {
                pattern.kind = OperandPatternKind::immediate16;
                pattern.high_byte_index = placeholder_digit(upper[2]);
                pattern.low_byte_index = placeholder_digit(upper[4]);
            } else if (upper.size() == 4 && upper[0] == '%' && placeholder_digit(upper[1]) > 0 &&
                       upper[2] == '%' && placeholder_digit(upper[3]) > 0) {
                pattern.kind = OperandPatternKind::address16;
                pattern.high_byte_index = placeholder_digit(upper[1]);
                pattern.low_byte_index = placeholder_digit(upper[3]);
            } else if (upper.size() == 4 && upper[0] == '0' && upper[1] >= '0' &&
                       upper[1] <= '7' && upper[2] == '%' && upper[3] == '1') {
                pattern.kind = OperandPatternKind::address11;
                pattern.addr11_page = upper[1] - '0';
                pattern.byte_index = 1;
            } else if (upper.size() == 3 && upper[0] == 'O' && upper[1] == '%' &&
                       placeholder_digit(upper[2]) > 0) {
                pattern.kind = OperandPatternKind::relative8;
                pattern.byte_index = placeholder_digit(upper[2]);
            } else if (upper.size() == 3 && upper[0] == '0' && upper[1] == '%' &&
                       placeholder_digit(upper[2]) > 0) {
                pattern.kind = OperandPatternKind::bit8;
                pattern.byte_index = placeholder_digit(upper[2]);
            } else if (upper.size() == 4 && upper[0] == '/' && upper[1] == '0' && upper[2] == '%' &&
                       placeholder_digit(upper[3]) > 0) {
                pattern.kind = OperandPatternKind::negated_bit8;
                pattern.byte_index = placeholder_digit(upper[3]);
            } else if (upper.size() == 2 && upper[0] == '%' && std::isdigit(static_cast<unsigned char>(upper[1])) != 0) {
                pattern.kind = OperandPatternKind::direct8;
                pattern.byte_index = upper[1] - '0';
            } else {
                throw std::runtime_error("unsupported opcode pattern token: " + token);
            }

            patterns.push_back(pattern);
        }
        return patterns;
    }

    std::vector<Descriptor> descriptors_;
};

namespace {

struct MatchAttempt {
    bool matched = false;
    std::vector<std::uint8_t> bytes;
    std::string reason;
};

class AssemblerEngine {
public:
    AssemblerEngine(const AssembleOptions& options, const OpcodeTable& opcode_table)
        : options_(options), opcode_table_(opcode_table) {}

    AssembleResult assemble_string(const std::string& source_name,
                                   std::string_view source,
                                   const fs::path& base_directory) {
        std::vector<fs::path> include_stack;
        process_source(source_name, std::string(source), base_directory, include_stack);
        if (!diagnostics_.empty()) {
            return make_result();
        }

        first_pass();
        if (!diagnostics_.empty()) {
            return make_result();
        }

        second_pass();
        return make_result();
    }

private:
    void add_error(const SourceLocation& location, const std::string& message) {
        diagnostics_.push_back({location.file, location.line, message});
    }

    AssembleResult make_result() const {
        AssembleResult result;
        result.image = image_;
        result.diagnostics = diagnostics_;
        for (const auto& [name, info] : symbols_) {
            result.symbols.insert({name, info.public_symbol});
        }
        result.image.symbols = result.symbols;
        return result;
    }

    std::optional<fs::path> resolve_include(const std::string& include_name, const fs::path& current_dir) const {
        fs::path requested(include_name);
        if (requested.is_absolute()) {
            if (fs::exists(requested)) {
                return requested.lexically_normal();
            }
            return std::nullopt;
        }

        std::vector<fs::path> search_dirs;
        if (!current_dir.empty()) {
            search_dirs.push_back(current_dir);
        }
        for (const auto& directory : detail::packaged_asm_directories())
            search_dirs.push_back(directory);
        for (const fs::path& dir : options_.include_directories) {
            search_dirs.push_back(dir);
        }

        for (const fs::path& dir : search_dirs) {
            const fs::path candidate = (dir / requested).lexically_normal();
            if (fs::exists(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    void process_source(const std::string& display_name,
                        const std::string& source,
                        const fs::path& base_directory,
                        std::vector<fs::path>& include_stack) {
        image_.sources[display_name] = source;
        // Keep the original text for debug matching while accepting the same
        // UTF-8 BOM that the editor strips when opening a source or include.
        const bool has_bom = source.compare(0, 3, "\xef\xbb\xbf") == 0;
        std::istringstream input(has_bom ? source.substr(3) : source);
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            process_line(display_name, line_number, line, base_directory, include_stack);
        }
    }

    void process_include(const SourceLocation& location,
                         const std::string& include_name,
                         const fs::path& base_directory,
                         std::vector<fs::path>& include_stack) {
        const auto resolved = resolve_include(include_name, base_directory);
        if (!resolved.has_value()) {
            add_error(location, "unable to resolve include '" + include_name + "'");
            return;
        }

        if (std::find(include_stack.begin(), include_stack.end(), *resolved) != include_stack.end()) {
            add_error(location, "include cycle detected for '" + resolved->string() + "'");
            return;
        }

        std::ifstream input(*resolved);
        if (!input) {
            add_error(location, "unable to open include '" + resolved->string() + "'");
            return;
        }

        const auto source = read_source_stream(input);
        if (!source) {
            add_error(location, "unable to read include '" + resolved->string() + "'");
            return;
        }
        include_stack.push_back(*resolved);
        process_source(resolved->string(), *source, resolved->parent_path(), include_stack);
        include_stack.pop_back();
    }

    void process_line(const std::string& file,
                      std::size_t line_number,
                      const std::string& line,
                      const fs::path& base_directory,
                      std::vector<fs::path>& include_stack) {
        const SourceLocation location{file, line_number};
        const std::string stripped = strip_comment(line);
        if (stripped.empty()) {
            return;
        }

        if (!stripped.empty() && stripped.front() == '$') {
            const std::string directive = to_upper_copy(trim_copy(std::string_view(stripped).substr(1)));
            if (directive.size() > 3 && directive.rfind("MOD", 0) == 0 &&
                std::all_of(directive.begin() + 3, directive.end(), [](unsigned char ch) {
                    return std::isalnum(ch) != 0 || ch == '_';
                })) {
                std::string module = directive;
                std::transform(module.begin(), module.end(), module.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                process_include(location, module, base_directory, include_stack);
                return;
            }

            const std::string upper = to_upper_copy(stripped);
            if (upper.rfind("$INCLUDE", 0) == 0) {
                std::string rest = trim_copy(std::string_view(stripped).substr(8));
                if (!rest.empty() && rest.front() == '(' && rest.back() == ')') {
                    rest = trim_copy(std::string_view(rest).substr(1, rest.size() - 2));
                }
                if (rest.empty()) {
                    add_error(location, "missing include path");
                    return;
                }
                process_include(location, rest, base_directory, include_stack);
                return;
            }

            add_error(location, "unsupported directive '" + stripped + "'");
            return;
        }

        parse_statement(location, stripped);
    }

    void parse_statement(const SourceLocation& location, const std::string& line) {
        std::string rest = line;
        std::optional<std::string> label;

        const std::size_t colon = rest.find(':');
        if (colon != std::string::npos) {
            const std::string candidate = trim_copy(std::string_view(rest).substr(0, colon));
            if (!candidate.empty() && is_identifier(candidate)) {
                label = candidate;
                rest = trim_copy(std::string_view(rest).substr(colon + 1));
            }
        }

        if (rest.empty()) {
            Statement statement;
            statement.location = location;
            statement.label = label;
            statement.kind = StatementKind::label_only;
            statements_.push_back(std::move(statement));
            return;
        }

        const std::size_t first_space = rest.find_first_of(" \t");
        const std::string first = first_space == std::string::npos ? rest : rest.substr(0, first_space);
        std::string remainder = first_space == std::string::npos ? std::string() : trim_copy(rest.substr(first_space + 1));

        const std::string upper_first = to_upper_copy(first);
        if (!label.has_value()) {
            const std::size_t second_space = remainder.find_first_of(" \t");
            const std::string second = second_space == std::string::npos ? remainder : remainder.substr(0, second_space);
            const std::string upper_second = to_upper_copy(second);
            if (is_identifier(first) &&
                (upper_second == "EQU" || upper_second == "DATA" || upper_second == "BIT")) {
                label = first;
                remainder = second_space == std::string::npos ? std::string() : trim_copy(remainder.substr(second_space + 1));

                Statement statement;
                statement.location = location;
                statement.label = label;
                statement.kind = StatementKind::definition;
                statement.keyword = upper_second;
                statement.arguments = {remainder};
                statement.definition_kind = upper_second == "BIT" ? SymbolKind::bit
                                              : upper_second == "DATA" ? SymbolKind::data
                                                                       : SymbolKind::equ;
                statements_.push_back(std::move(statement));
                return;
            }
        }

        Statement statement;
        statement.location = location;
        statement.label = label;
        statement.keyword = upper_first;

        if (upper_first == "ORG") {
            statement.kind = StatementKind::org;
            statement.arguments = {remainder};
        } else if (upper_first == "DB") {
            statement.kind = StatementKind::db;
            statement.arguments = split_top_level(remainder, ',');
        } else if (upper_first == "DW") {
            statement.kind = StatementKind::dw;
            statement.arguments = split_top_level(remainder, ',');
        } else if (upper_first == "END") {
            statement.kind = StatementKind::end_directive;
        } else {
            statement.kind = StatementKind::instruction;
            statement.arguments = remainder.empty() ? std::vector<std::string>{} : split_top_level(remainder, ',');
        }

        statements_.push_back(std::move(statement));
    }

    bool define_symbol(const std::string& raw_name,
                       std::uint16_t value,
                       SymbolKind kind,
                       const SourceLocation& location) {
        const std::string name = to_upper_copy(raw_name);
        if (symbols_.find(name) != symbols_.end()) {
            add_error(location, "duplicate symbol '" + raw_name + "'");
            return false;
        }
        symbols_[name] = {{value, kind}, location};
        return true;
    }

    ExpressionEvaluator::Result evaluate_expression(const std::string& expression, bool allow_unresolved) const {
        return ExpressionEvaluator(expression, symbols_, allow_unresolved).evaluate();
    }

    MatchAttempt try_match(const OpcodeTable::Descriptor& descriptor,
                           const std::vector<ParsedOperand>& operands,
                           std::uint16_t pc,
                           bool allow_unresolved) const {
        MatchAttempt attempt;
        if (descriptor.operands.size() != operands.size()) {
            attempt.reason = "operand count mismatch";
            return attempt;
        }

        attempt.bytes.assign(descriptor.length, 0);
        if (!attempt.bytes.empty()) {
            attempt.bytes[0] = descriptor.opcode;
        }

        for (std::size_t i = 0; i < operands.size(); ++i) {
            const ParsedOperand& operand = operands[i];
            const OperandPattern& pattern = descriptor.operands[i];
            const auto symbol_kind = [&]() -> std::optional<SymbolKind> {
                if (!operand.simple_symbol.has_value()) {
                    return std::nullopt;
                }
                const auto it = symbols_.find(*operand.simple_symbol);
                if (it == symbols_.end()) {
                    return std::nullopt;
                }
                return it->second.public_symbol.kind;
            };

            auto evaluate = [&](std::int64_t min_value, std::int64_t max_value, bool immediate_ok = false)
                -> std::optional<std::int64_t> {
                if (immediate_ok) {
                    if (operand.kind != ParsedOperandKind::immediate) {
                        attempt.reason = "operand " + std::to_string(i + 1) + " must be immediate";
                        return std::nullopt;
                    }
                } else if (operand.kind != ParsedOperandKind::expression &&
                           operand.kind != ParsedOperandKind::negated_bit) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be an expression";
                    return std::nullopt;
                }

                const ExpressionEvaluator::Result eval =
                    evaluate_expression(operand.text, allow_unresolved);
                std::string error;
                const auto value = checked_value(eval, min_value, max_value, error);
                if (!value.has_value()) {
                    if (allow_unresolved && eval.ok && eval.unresolved) {
                        return std::int64_t{0};
                    }
                    attempt.reason = "operand " + std::to_string(i + 1) + ": " + error;
                    return std::nullopt;
                }
                return value;
            };

            switch (pattern.kind) {
            case OperandPatternKind::accumulator:
                if (operand.kind != ParsedOperandKind::accumulator) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be A";
                    return attempt;
                }
                break;
            case OperandPatternKind::carry:
                if (operand.kind != ParsedOperandKind::carry) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be C";
                    return attempt;
                }
                break;
            case OperandPatternKind::ab:
                if (operand.kind != ParsedOperandKind::ab) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be AB";
                    return attempt;
                }
                break;
            case OperandPatternKind::dptr:
                if (operand.kind != ParsedOperandKind::dptr) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be DPTR";
                    return attempt;
                }
                break;
            case OperandPatternKind::at_dptr:
                if (operand.kind != ParsedOperandKind::at_dptr) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be @DPTR";
                    return attempt;
                }
                break;
            case OperandPatternKind::reg:
                if (operand.kind != ParsedOperandKind::reg || operand.reg_index != pattern.reg_index) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " has wrong register";
                    return attempt;
                }
                break;
            case OperandPatternKind::indirect_reg:
                if (operand.kind != ParsedOperandKind::indirect_reg || operand.reg_index != pattern.reg_index) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " has wrong indirect register";
                    return attempt;
                }
                break;
            case OperandPatternKind::at_a_plus_dptr:
                if (operand.kind != ParsedOperandKind::at_a_plus_dptr) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be @A+DPTR";
                    return attempt;
                }
                break;
            case OperandPatternKind::at_a_plus_pc:
                if (operand.kind != ParsedOperandKind::at_a_plus_pc) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be @A+PC";
                    return attempt;
                }
                break;
            case OperandPatternKind::direct8: {
                if (operand.kind != ParsedOperandKind::expression) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be a direct address";
                    return attempt;
                }
                const auto kind = symbol_kind();
                if (kind.has_value() && *kind == SymbolKind::bit) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " uses bit symbol where direct address is required";
                    return attempt;
                }
                const auto value = evaluate(0, 0xff);
                if (!value.has_value()) {
                    return attempt;
                }
                attempt.bytes[pattern.byte_index] = static_cast<std::uint8_t>(*value);
                break;
            }
            case OperandPatternKind::immediate8: {
                const auto value = evaluate(0, 0xff, true);
                if (!value.has_value()) {
                    return attempt;
                }
                attempt.bytes[pattern.byte_index] = static_cast<std::uint8_t>(*value);
                break;
            }
            case OperandPatternKind::immediate16: {
                const auto value = evaluate(0, 0xffff, true);
                if (!value.has_value()) {
                    return attempt;
                }
                attempt.bytes[pattern.high_byte_index] = static_cast<std::uint8_t>((*value >> 8) & 0xff);
                attempt.bytes[pattern.low_byte_index] = static_cast<std::uint8_t>(*value & 0xff);
                break;
            }
            case OperandPatternKind::bit8: {
                if (operand.kind != ParsedOperandKind::expression) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be a bit address";
                    return attempt;
                }
                const auto kind = symbol_kind();
                if (kind.has_value() && *kind != SymbolKind::bit) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " uses non-bit symbol where bit address is required";
                    return attempt;
                }
                const auto value = evaluate(0, 0xff);
                if (!value.has_value()) {
                    return attempt;
                }
                attempt.bytes[pattern.byte_index] = static_cast<std::uint8_t>(*value);
                break;
            }
            case OperandPatternKind::negated_bit8: {
                if (operand.kind != ParsedOperandKind::negated_bit) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be /bit";
                    return attempt;
                }
                const auto kind = symbol_kind();
                if (kind.has_value() && *kind != SymbolKind::bit) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " uses non-bit symbol where bit address is required";
                    return attempt;
                }
                const auto value = evaluate(0, 0xff);
                if (!value.has_value()) {
                    return attempt;
                }
                attempt.bytes[pattern.byte_index] = static_cast<std::uint8_t>(*value);
                break;
            }
            case OperandPatternKind::relative8: {
                if (operand.kind != ParsedOperandKind::expression) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be a relative target";
                    return attempt;
                }
                const ExpressionEvaluator::Result eval =
                    evaluate_expression(operand.text, allow_unresolved);
                if (allow_unresolved && eval.ok && eval.unresolved) {
                    attempt.bytes[pattern.byte_index] = 0;
                    break;
                }
                std::string error;
                const auto target = checked_value(eval, 0, 0xffff, error);
                if (!target.has_value()) {
                    attempt.reason = "operand " + std::to_string(i + 1) + ": " + error;
                    return attempt;
                }
                // Relative branches add their signed displacement to a 16-bit
                // PC, so both the next PC and the subtraction wrap at 64KB.
                const auto next_pc = static_cast<std::uint16_t>(pc + descriptor.length);
                const auto offset = static_cast<std::uint16_t>(*target - next_pc);
                if (offset > 0x007f && offset < 0xff80) {
                    attempt.reason = "relative branch out of range";
                    return attempt;
                }
                attempt.bytes[pattern.byte_index] = static_cast<std::uint8_t>(offset & 0xff);
                break;
            }
            case OperandPatternKind::address16: {
                if (operand.kind != ParsedOperandKind::expression) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be a 16-bit address";
                    return attempt;
                }
                const auto value = evaluate(0, 0xffff);
                if (!value.has_value()) {
                    return attempt;
                }
                attempt.bytes[pattern.high_byte_index] = static_cast<std::uint8_t>((*value >> 8) & 0xff);
                attempt.bytes[pattern.low_byte_index] = static_cast<std::uint8_t>(*value & 0xff);
                break;
            }
            case OperandPatternKind::address11: {
                if (operand.kind != ParsedOperandKind::expression) {
                    attempt.reason = "operand " + std::to_string(i + 1) + " must be an 11-bit address";
                    return attempt;
                }
                const ExpressionEvaluator::Result eval =
                    evaluate_expression(operand.text, allow_unresolved);
                if (allow_unresolved && eval.ok && eval.unresolved) {
                    attempt.bytes[pattern.byte_index] = 0;
                    break;
                }
                std::string error;
                const auto value = checked_value(eval, 0, 0xffff, error);
                if (!value.has_value()) {
                    attempt.reason = "operand " + std::to_string(i + 1) + ": " + error;
                    return attempt;
                }
                const std::uint16_t target = static_cast<std::uint16_t>(*value);
                const std::uint16_t next_pc = static_cast<std::uint16_t>(pc + descriptor.length);
                if ((target & 0xf800u) != (next_pc & 0xf800u)) {
                    attempt.reason = "11-bit jump target crosses 2KB page boundary";
                    return attempt;
                }
                if (((target >> 8) & 0x07u) != static_cast<std::uint16_t>(pattern.addr11_page)) {
                    attempt.reason = "11-bit jump target requires different opcode page";
                    return attempt;
                }
                attempt.bytes[pattern.byte_index] = static_cast<std::uint8_t>(target & 0xff);
                break;
            }
            }
        }

        attempt.matched = true;
        return attempt;
    }

    const OpcodeTable::Descriptor* select_descriptor(const Statement& statement,
                                                     std::uint16_t pc,
                                                     bool allow_unresolved,
                                                     std::vector<std::uint8_t>* encoded_bytes,
                                                     std::string* error_message) const {
        std::vector<ParsedOperand> operands;
        operands.reserve(statement.arguments.size());
        for (const std::string& argument : statement.arguments) {
            operands.push_back(parse_operand(argument));
        }

        const auto descriptors = opcode_table_.lookup(statement.keyword);
        if (descriptors.empty()) {
            if (error_message != nullptr) {
                *error_message = "unknown mnemonic '" + statement.keyword + "'";
            }
            return nullptr;
        }

        std::string first_reason = "unsupported operand form";
        for (const OpcodeTable::Descriptor* descriptor : descriptors) {
            MatchAttempt attempt = try_match(*descriptor, operands, pc, allow_unresolved);
            if (attempt.matched) {
                if (encoded_bytes != nullptr) {
                    *encoded_bytes = std::move(attempt.bytes);
                }
                return descriptor;
            }
            if (!attempt.reason.empty()) {
                first_reason = attempt.reason;
            }
        }

        if (error_message != nullptr) {
            *error_message = first_reason;
        }
        return nullptr;
    }

    void first_pass() {
        std::uint16_t pc = 0;
        for (const Statement& statement : statements_) {
            if (statement.kind == StatementKind::end_directive) {
                break;
            }

            if (statement.label.has_value() && statement.kind != StatementKind::definition) {
                define_symbol(*statement.label, pc, SymbolKind::label, statement.location);
            }

            switch (statement.kind) {
            case StatementKind::label_only:
                break;
            case StatementKind::definition: {
                const ExpressionEvaluator::Result eval =
                    evaluate_expression(statement.arguments.empty() ? std::string() : statement.arguments.front(), false);
                std::string error;
                const auto value = checked_value(eval, 0, 0xffff, error);
                if (!value.has_value()) {
                    add_error(statement.location, error.empty() ? "invalid definition value" : error);
                    break;
                }
                if (statement.label.has_value()) {
                    define_symbol(*statement.label,
                                  static_cast<std::uint16_t>(*value),
                                  statement.definition_kind,
                                  statement.location);
                }
                break;
            }
            case StatementKind::org: {
                const ExpressionEvaluator::Result eval =
                    evaluate_expression(statement.arguments.empty() ? std::string() : statement.arguments.front(), false);
                std::string error;
                const auto value = checked_value(eval, 0, 0xffff, error);
                if (!value.has_value()) {
                    add_error(statement.location, error.empty() ? "invalid ORG value" : error);
                    break;
                }
                pc = static_cast<std::uint16_t>(*value);
                break;
            }
            case StatementKind::db:
                pc = static_cast<std::uint16_t>(pc + statement.arguments.size());
                break;
            case StatementKind::dw:
                pc = static_cast<std::uint16_t>(pc + statement.arguments.size() * 2u);
                break;
            case StatementKind::instruction: {
                std::string error;
                const OpcodeTable::Descriptor* descriptor =
                    select_descriptor(statement, pc, true, nullptr, &error);
                if (descriptor == nullptr) {
                    add_error(statement.location, error);
                    break;
                }
                pc = static_cast<std::uint16_t>(pc + descriptor->length);
                break;
            }
            case StatementKind::end_directive:
                break;
            }
        }
    }

    void emit_byte(const SourceLocation& location, std::uint16_t address, std::uint8_t value) {
        const auto it = image_.bytes.find(address);
        if (it != image_.bytes.end() && it->second != value) {
            add_error(location, "overlapping output at address " + std::to_string(address));
            return;
        }
        image_.bytes[address] = value;
        image_.sourceLines.try_emplace(address, AssemblyImage::SourceLine{location.file, location.line});
    }

    void second_pass() {
        std::uint16_t pc = 0;
        for (const Statement& statement : statements_) {
            if (statement.kind == StatementKind::end_directive) {
                break;
            }

            switch (statement.kind) {
            case StatementKind::label_only:
            case StatementKind::definition:
                break;
            case StatementKind::org: {
                const ExpressionEvaluator::Result eval =
                    evaluate_expression(statement.arguments.empty() ? std::string() : statement.arguments.front(), false);
                std::string error;
                const auto value = checked_value(eval, 0, 0xffff, error);
                if (value.has_value()) {
                    pc = static_cast<std::uint16_t>(*value);
                }
                break;
            }
            case StatementKind::db:
                for (const std::string& argument : statement.arguments) {
                    const ExpressionEvaluator::Result eval = evaluate_expression(argument, false);
                    std::string error;
                    const auto value = checked_value(eval, 0, 0xff, error);
                    if (!value.has_value()) {
                        add_error(statement.location, error.empty() ? "invalid DB operand" : error);
                        return;
                    }
                    emit_byte(statement.location, pc, static_cast<std::uint8_t>(*value));
                    ++pc;
                }
                break;
            case StatementKind::dw:
                for (const std::string& argument : statement.arguments) {
                    const ExpressionEvaluator::Result eval = evaluate_expression(argument, false);
                    std::string error;
                    const auto value = checked_value(eval, 0, 0xffff, error);
                    if (!value.has_value()) {
                        add_error(statement.location, error.empty() ? "invalid DW operand" : error);
                        return;
                    }
                    emit_byte(statement.location, pc,
                              static_cast<std::uint8_t>((*value >> 8) & 0xff));
                    emit_byte(statement.location, static_cast<std::uint16_t>(pc + 1),
                              static_cast<std::uint8_t>(*value & 0xff));
                    pc = static_cast<std::uint16_t>(pc + 2);
                }
                break;
            case StatementKind::instruction: {
                std::vector<std::uint8_t> encoded;
                std::string error;
                const OpcodeTable::Descriptor* descriptor =
                    select_descriptor(statement, pc, false, &encoded, &error);
                if (descriptor == nullptr) {
                    add_error(statement.location, error);
                    return;
                }
                image_.instructionAddresses.insert(pc);
                for (std::size_t i = 0; i < encoded.size(); ++i) {
                    emit_byte(statement.location, static_cast<std::uint16_t>(pc + i), encoded[i]);
                }
                pc = static_cast<std::uint16_t>(pc + descriptor->length);
                break;
            }
            case StatementKind::end_directive:
                return;
            }
        }
    }

    const AssembleOptions& options_;
    const OpcodeTable& opcode_table_;
    std::vector<Statement> statements_;
    std::map<std::string, SymbolInfo> symbols_;
    AssemblyImage image_;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace

Assembler::Assembler(AssembleOptions options) : options_(std::move(options)) {}

AssembleResult Assembler::assemble_file(const fs::path& source_path) const {
    std::error_code error;
    if (!fs::is_regular_file(source_path, error)) {
        AssembleResult result;
        result.diagnostics.push_back({source_path.string(), 0, "unable to open source file"});
        return result;
    }
    std::ifstream input(source_path);
    if (!input) {
        AssembleResult result;
        result.diagnostics.push_back({source_path.string(), 0, "unable to open source file"});
        return result;
    }

    const auto source = read_source_stream(input);
    if (!source) {
        AssembleResult result;
        result.diagnostics.push_back({source_path.string(), 0, "unable to read source file"});
        return result;
    }
    return assemble_string(source_path.string(), *source, source_path.parent_path());
}

AssembleResult Assembler::assemble_string(const std::string& source_name,
                                         std::string_view source,
                                         const fs::path& base_directory) const {
    AssemblerEngine engine(options_, OpcodeTable::builtin());
    return engine.assemble_string(source_name, source, base_directory);
}

std::vector<std::string> opcode_mnemonics() {
    return OpcodeTable::builtin().mnemonics();
}

} // namespace emu51_asm
