// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_asm_editor.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "assembler.h"

namespace {

bool isAsciiAlpha(ImWchar ch)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool isAsciiDigit(ImWchar ch)
{
	return ch >= '0' && ch <= '9';
}

bool isAsmIdentifierStart(ImWchar ch)
{
	return isAsciiAlpha(ch) || ch == '_' || ch == '.' || ch == '$' || ch == '?';
}

bool isAsmIdentifierChar(ImWchar ch)
{
	return isAsmIdentifierStart(ch) || isAsciiDigit(ch);
}

bool isAsmHexDigit(ImWchar ch)
{
	return isAsciiDigit(ch) || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

std::string lowerCopy(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return text;
}

TextEditor::Iterator getAsmIdentifier(TextEditor::Iterator start, TextEditor::Iterator end)
{
	if (start == end || !isAsmIdentifierStart(*start))
		return start;

	TextEditor::Iterator it = start;
	while (it != end && isAsmIdentifierChar(*it))
		++it;
	return it;
}

TextEditor::Iterator getAsmNumber(TextEditor::Iterator start, TextEditor::Iterator end)
{
	if (start == end || !isAsciiDigit(*start))
		return start;

	TextEditor::Iterator it = start;
	if (*it == '0') {
		TextEditor::Iterator next = it;
		++next;
		if (next != end && (*next == 'x' || *next == 'X')) {
			++next;
			TextEditor::Iterator digits = next;
			while (next != end && isAsmHexDigit(*next))
				++next;
			return next == digits ? start : next;
		}
	}

	while (it != end && isAsmHexDigit(*it))
		++it;
	if (it != end && (*it == 'h' || *it == 'H' || *it == 'b' || *it == 'B'))
		++it;
	return it;
}

bool isAsmPunctuation(ImWchar ch)
{
	switch (ch) {
	case ':':
	case ',':
	case '#':
	case '@':
	case '+':
	case '-':
	case '*':
	case '/':
	case '(':
	case ')':
	case '[':
	case ']':
		return true;
	default:
		return false;
	}
}

}  // namespace

const TextEditor::Language* emu51AsmEditorLanguage()
{
	static bool initialized = false;
	static TextEditor::Language language;

	if (!initialized) {
		language.name = "8051 Assembly";
		language.caseSensitive = false;
		language.singleLineComment = ";";
		language.getIdentifier = getAsmIdentifier;
		language.getNumber = getAsmNumber;
		language.isPunctuation = isAsmPunctuation;

		for (const std::string& mnemonic : emu51_asm::opcode_mnemonics())
			language.keywords.insert(lowerCopy(mnemonic));

		static const char* const directives[] = {
			"org", "db", "dw", "end", "equ", "data", "bit", "$mod851", "$include",
		};
		for (const char* directive : directives)
			language.declarations.insert(directive);

		static const char* const identifiers[] = {
			"a", "b", "ab", "c", "dptr", "pc", "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
		};
		for (const char* identifier : identifiers)
			language.identifiers.insert(identifier);

		initialized = true;
	}

	return &language;
}

void emu51ConfigureAsmEditor(TextEditor& editor)
{
	editor.SetLanguage(emu51AsmEditorLanguage());
	editor.SetTabSize(4);
	editor.SetInsertSpacesOnTabs(true);
	editor.SetAutoIndentEnabled(true);
	editor.SetShowLineNumbersEnabled(true);
	editor.SetShowScrollbarMiniMapEnabled(true);
	editor.SetShowMatchingBrackets(true);
	editor.SetCompletePairedGlyphs(false);
}
