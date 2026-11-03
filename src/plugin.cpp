#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <string_view>
#include "rules.hpp"

// Notepad++ x64 plugin ABI and the small Scintilla message subset used here.
// These local declarations avoid shipping or depending on the SDK at runtime.
struct NppData { HWND main, first, second; };
struct ShortcutKey { bool ctrl, alt, shift; unsigned char key; };
struct FuncItem { wchar_t name[64]; void (__cdecl* callback)(); int command; bool checked; ShortcutKey* shortcut; };
struct SCNotification;
static_assert(sizeof(void*) == 8 && sizeof(NppData) == 24 && sizeof(FuncItem) == 152);

namespace {
enum : UINT {
    SCI_GETCURRENTPOS=2008, SCI_GETANCHOR=2009, SCI_GETUNDOCOLLECTION=2019,
    SCI_GETEOLMODE=2030, SCI_GETTEXTRANGEFULL=2039, SCI_BEGINUNDOACTION=2078,
    SCI_ENDUNDOACTION=2079, SCI_GETLINEENDPOSITION=2136, SCI_GETCODEPAGE=2137,
    SCI_GETREADONLY=2140, SCI_GETLINECOUNT=2154, SCI_SETSEL=2160,
    SCI_LINEFROMPOSITION=2166, SCI_POSITIONFROMLINE=2167, SCI_GETTEXTLENGTH=2183,
    SCI_GETOVERTYPE=2187, SCI_GETTARGETSTART=2191, SCI_GETTARGETEND=2193,
    SCI_REPLACETARGET=2194, SCI_GETDOCPOINTER=2357, SCI_GETSELECTIONMODE=2423,
    SCI_POSITIONBEFORE=2417, SCI_POSITIONAFTER=2418, SCI_GETSELECTIONS=2570,
    SCI_GETSELECTIONNCARETVIRTUALSPACE=2581, SCI_GETSELECTIONNANCHORVIRTUALSPACE=2583,
    SCI_SETEMPTYSELECTION=2556, SCI_SETTARGETRANGE=2686,
    SCI_GETTARGETSTARTVIRTUALSPACE=2729, SCI_GETTARGETENDVIRTUALSPACE=2731,
    SCI_SETTARGETSTARTVIRTUALSPACE=2728, SCI_SETTARGETENDVIRTUALSPACE=2730
};
constexpr UINT_PTR subclass_id = 0x434e;
constexpr UINT npp_ready = 1001, npp_shutdown = 1009;
constexpr UINT sci_paste = 2179, npp_paste = 42005;
NppData host{};
bool editor_attached[2]{};
bool main_attached{};
thread_local bool busy{};

struct Range { std::intptr_t from, to; };
struct TextRange { Range range; char* text; };
static_assert(sizeof(TextRange) == 24);
LRESULT sci(HWND hwnd, UINT message, WPARAM w = 0, LPARAM l = 0) noexcept {
    return SendMessageW(hwnd, message, w, l);
}
bool same_doc(HWND editor, LRESULT doc) noexcept {
    return doc && sci(editor, SCI_GETDOCPOINTER) == doc;
}
bool read(HWND editor, LRESULT doc, std::intptr_t from, std::intptr_t to, std::string& out) {
    if (from < 0 || to < from || to - from > 32 || !same_doc(editor, doc)) return false;
    std::array<char, 33> buffer{};
    buffer[static_cast<std::size_t>(to - from)] = '\x7f';
    TextRange range{{from, to}, buffer.data()};
    if (sci(editor, SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range)) != to - from
        || buffer[static_cast<std::size_t>(to-from)] != '\0' || !same_doc(editor, doc)) return false;
    out.assign(buffer.data(), static_cast<std::size_t>(to-from));
    return true;
}
bool byte_at(HWND editor, LRESULT doc, std::intptr_t pos, char& value) {
    std::string content;
    if (!read(editor, doc, pos, pos+1, content)) return false;
    value = content[0];
    return true;
}

enum class Input { lower, upper, space, deletion, paste, enter };
bool modifiers_ok(Input mode) noexcept {
    constexpr int modifiers[]{VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN,VK_LSHIFT,VK_RSHIFT};
    for (int key : modifiers) {
        if (mode == Input::paste && (key == VK_LCONTROL || key == VK_RCONTROL || key == VK_LSHIFT || key == VK_RSHIFT)) continue;
        if ((mode == Input::upper || mode == Input::space) && (key == VK_LSHIFT || key == VK_RSHIFT)) continue;
        if (GetKeyState(key) & 0x8000) return false;
    }
    if (mode == Input::lower && ((GetKeyState(VK_CAPITAL) & 1) ||
        reinterpret_cast<std::uintptr_t>(GetKeyboardLayout(GetCurrentThreadId())) != 0x04090409u)) return false;
    return true;
}
bool focused(HWND editor) noexcept {
    if (!editor || !host.main || GetForegroundWindow() != host.main || GetFocus() != editor
        || (editor != host.first && editor != host.second) || !IsChild(host.main, editor)
        || GetWindowThreadProcessId(editor, nullptr) != GetCurrentThreadId()
        || GetWindowThreadProcessId(host.main, nullptr) != GetCurrentThreadId()) return false;
    wchar_t name[32]{};
    return GetClassNameW(editor, name, 32) && std::wcscmp(name, L"Scintilla") == 0;
}
struct Context {
    HWND editor{};
    LRESULT doc{}, len{}, caret{}, anchor{}, line{}, line_start{}, line_end{};
};
bool capture(HWND editor, Input mode, Context& result) noexcept {
    if (!focused(editor) || !modifiers_ok(mode)) return false;
    if (sci(editor, SCI_GETREADONLY) || sci(editor, SCI_GETOVERTYPE)
        || sci(editor, SCI_GETUNDOCOLLECTION) != 1 || sci(editor, SCI_GETSELECTIONS) != 1
        || sci(editor, SCI_GETSELECTIONMODE) != 0
        || sci(editor, SCI_GETSELECTIONNCARETVIRTUALSPACE) != 0
        || sci(editor, SCI_GETSELECTIONNANCHORVIRTUALSPACE) != 0) return false;
    const auto cp = sci(editor, SCI_GETCODEPAGE);
    if (cp != 0 && cp != 65001) return false;
    result.editor = editor;
    result.doc = sci(editor, SCI_GETDOCPOINTER);
    result.len = sci(editor, SCI_GETTEXTLENGTH);
    result.caret = sci(editor, SCI_GETCURRENTPOS);
    result.anchor = sci(editor, SCI_GETANCHOR);
    if (!result.doc || result.len < 0 || result.caret < 0 || result.caret > result.len
        || result.anchor < 0 || result.anchor > result.len
        || (mode != Input::deletion && result.caret != result.anchor)) return false;
    result.line = sci(editor, SCI_LINEFROMPOSITION, static_cast<WPARAM>(result.caret));
    result.line_start = sci(editor, SCI_POSITIONFROMLINE, static_cast<WPARAM>(result.line));
    result.line_end = sci(editor, SCI_GETLINEENDPOSITION, static_cast<WPARAM>(result.line));
    return result.line >= 0 && result.line_start >= 0 && result.line_start <= result.caret
        && result.caret <= result.line_end && result.line_end <= result.len
        && same_doc(editor, result.doc) && focused(editor);
}
bool unchanged(const Context& original, Input mode) noexcept {
    Context current{};
    return capture(original.editor, mode, current)
        && current.doc == original.doc && current.len == original.len
        && current.caret == original.caret && current.anchor == original.anchor
        && current.line == original.line && current.line_start == original.line_start
        && current.line_end == original.line_end;
}
bool eligible_prefix(std::string_view bytes, std::intptr_t column) noexcept {
    if (column == 0) return true;
    if (column < 2 || bytes.size() < 2) return false;
    char before = bytes[bytes.size()-2];
    char last = bytes.back();
    return last == ' ' && (before == '.' || before == '?' || before == ':' || before == '!'
        || (before == '-' && column == 2));
}
bool eligible_at(HWND editor, LRESULT doc, std::intptr_t position, bool& answer) {
    if (position < 0 || !same_doc(editor, doc)) return false;
    auto line = sci(editor, SCI_LINEFROMPOSITION, static_cast<WPARAM>(position));
    auto start = sci(editor, SCI_POSITIONFROMLINE, static_cast<WPARAM>(line));
    if (line < 0 || start < 0 || start > position) return false;
    std::string prior;
    if (!read(editor, doc, (std::max)(start, position-2), position, prior)) return false;
    answer = eligible_prefix(prior, position-start);
    return true;
}
bool boundary(HWND editor, std::intptr_t position) noexcept {
    if (position == 0) return true;
    auto prior = sci(editor, SCI_POSITIONBEFORE, static_cast<WPARAM>(position));
    return prior >= 0 && prior < position && sci(editor, SCI_POSITIONAFTER, static_cast<WPARAM>(prior)) == position;
}

// The undo action is attached to the active Scintilla document. No hidden view,
// custom document leasing, or diagnostic state machinery is necessary.
struct Undo {
    HWND editor{};
    bool active{};
    explicit Undo(HWND window) noexcept : editor(window), active(true) { sci(editor, SCI_BEGINUNDOACTION); }
    ~Undo() noexcept { if (active) sci(editor, SCI_ENDUNDOACTION); }
    Undo(const Undo&) = delete;
    Undo& operator=(const Undo&) = delete;
};
struct Tx {
    HWND editor{}, receiver{};
    UINT message{};
    WPARAM input{};
    LPARAM detail{};
    bool forwarded{};
    LRESULT native_result{};
    Tx(HWND editor_, HWND receiver_, UINT msg, WPARAM w, LPARAM l)
      : editor(editor_), receiver(receiver_), message(msg), input(w), detail(l) { busy = true; }
    ~Tx() { busy = false; }
    LRESULT forward(WPARAM character) noexcept {
        if (forwarded) return native_result;
        forwarded = true;
        native_result = DefSubclassProc(receiver, message, character, detail);
        return native_result;
    }
};

// Keep the caret and the host's target metadata after our checked small edit.
// Callers must verify context/document before and after invoking this function.
bool replace(HWND editor, LRESULT doc, std::intptr_t start, std::intptr_t end,
             std::string_view expected, std::string_view replacement, std::intptr_t caret) {
    std::string actual;
    if (end < start || end-start != static_cast<std::intptr_t>(expected.size())
        || !read(editor, doc, start, end, actual) || actual != expected) return false;
    const auto old_start = sci(editor, SCI_GETTARGETSTART), old_end = sci(editor, SCI_GETTARGETEND);
    const auto old_sv = sci(editor, SCI_GETTARGETSTARTVIRTUALSPACE), old_ev = sci(editor, SCI_GETTARGETENDVIRTUALSPACE);
    sci(editor, SCI_SETTARGETRANGE, static_cast<WPARAM>(start), end);
    const auto written = sci(editor, SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    if (!same_doc(editor, doc)) return false;
    const auto new_len = sci(editor, SCI_GETTEXTLENGTH);
    if (caret >= 0 && caret <= new_len) sci(editor, SCI_SETEMPTYSELECTION, static_cast<WPARAM>(caret));
    // Adjust saved in-document target endpoints for a length-changing edit.
    const auto adjust = [&](LRESULT p) noexcept -> LRESULT {
        if (p < 0 || p <= start) return p;
        if (p < end) return start + static_cast<LRESULT>(replacement.size());
        return p + static_cast<LRESULT>(replacement.size()) - static_cast<LRESULT>(expected.size());
    };
    const auto restore_start = adjust(old_start), restore_end = adjust(old_end);
    if (restore_start >= -1 && restore_end >= -1 && restore_start <= new_len && restore_end <= new_len) {
        sci(editor, SCI_SETTARGETRANGE, static_cast<WPARAM>(restore_start), restore_end);
        sci(editor, SCI_SETTARGETSTARTVIRTUALSPACE, static_cast<WPARAM>(old_sv));
        sci(editor, SCI_SETTARGETENDVIRTUALSPACE, static_cast<WPARAM>(old_ev));
    }
    return written == static_cast<LRESULT>(replacement.size())
        && read(editor, doc, start, start + static_cast<std::intptr_t>(replacement.size()), actual)
        && actual == replacement && sci(editor, SCI_GETCURRENTPOS) == caret
        && sci(editor, SCI_GETANCHOR) == caret;
}
bool replace_letter(HWND editor, LRESULT doc, std::intptr_t at, char old_char, char new_char, std::intptr_t caret) {
    return replace(editor, doc, at, at+1, std::string_view(&old_char, 1),
                   std::string_view(&new_char, 1), caret);
}

LRESULT typed(Tx& tx) {
    const auto mode = noteplug::lower(static_cast<unsigned char>(tx.input)) ? Input::lower : Input::upper;
    Context before{};
    if (!capture(tx.editor, mode, before)) return tx.forward(tx.input);
    bool cap = false;
    if (mode == Input::lower && !eligible_at(tx.editor, before.doc, before.caret, cap)) return tx.forward(tx.input);
    const char incoming = static_cast<char>(tx.input);
    const char output = cap ? noteplug::uppercase(incoming) : incoming;
    std::intptr_t successor = -1;
    char original{};
    if (noteplug::upper(static_cast<unsigned char>(output)) && before.caret < before.line_end) {
        char next{};
        if (!byte_at(tx.editor, before.doc, before.caret, next)) return tx.forward(tx.input);
        successor = before.caret;
        if (next == ' ') {
            ++successor;
            if (successor >= before.line_end || !byte_at(tx.editor, before.doc, successor, next)) successor = -1;
        }
        if (!noteplug::upper(static_cast<unsigned char>(next))) successor = -1;
        else original = next;
    }
    if (!unchanged(before, mode)) return tx.forward(tx.input);
    if (successor < 0) return tx.forward(static_cast<unsigned char>(output));
    Undo undo(tx.editor);
    if (!unchanged(before, mode)) return tx.forward(tx.input);
    const auto result = tx.forward(static_cast<unsigned char>(output));
    Context after{};
    char inserted{}, existing{};
    if (capture(tx.editor, mode, after) && after.doc == before.doc
        && after.len == before.len + 1 && after.caret == before.caret + 1
        && after.anchor == after.caret && after.line == before.line
        && byte_at(tx.editor, before.doc, before.caret, inserted) && inserted == output
        && byte_at(tx.editor, before.doc, successor+1, existing) && existing == original) {
        (void)replace_letter(tx.editor, before.doc, successor+1, original,
                             noteplug::lowercase(original), after.caret);
    }
    return result;
}

LRESULT deletion(Tx& tx) {
    Context before{};
    if (!capture(tx.editor, Input::deletion, before)) return tx.forward(tx.input);
    const bool back = tx.input == VK_BACK;
    std::intptr_t first = (std::min)(before.caret, before.anchor);
    std::intptr_t last = (std::max)(before.caret, before.anchor);
    if (first == last) {
        if ((back && before.caret == 0) || (!back && before.caret == before.len)) return tx.forward(tx.input);
        const auto adjacent = sci(tx.editor, back ? SCI_POSITIONBEFORE : SCI_POSITIONAFTER,
                                  static_cast<WPARAM>(before.caret));
        if (adjacent < 0 || adjacent > before.len || adjacent == before.caret
            || sci(tx.editor, back ? SCI_POSITIONAFTER : SCI_POSITIONBEFORE,
                   static_cast<WPARAM>(adjacent)) != before.caret) return tx.forward(tx.input);
        first = back ? adjacent : before.caret;
        last = back ? before.caret : adjacent;
        // Respect CRLF as one native EOL even if the adapter reports only a byte.
        std::string adjacent_bytes;
        if (!back && last-first == 1 && first+2 <= before.len
            && read(tx.editor, before.doc, first, first+2, adjacent_bytes)
            && adjacent_bytes == "\r\n") ++last;
        if (back && last-first == 1 && first > 0
            && read(tx.editor, before.doc, first-1, last, adjacent_bytes)
            && adjacent_bytes == "\r\n") --first;
        if (last-first < 1 || last-first > 4) return tx.forward(tx.input);
    } else if (!boundary(tx.editor, first) || !boundary(tx.editor, last)) return tx.forward(tx.input);
    if (last >= before.len) return tx.forward(tx.input);
    std::intptr_t old_letter = last;
    char survivor{};
    if (!byte_at(tx.editor, before.doc, old_letter, survivor)) return tx.forward(tx.input);
    if (survivor == ' ') {
        if (++old_letter >= before.len || !byte_at(tx.editor, before.doc, old_letter, survivor)) return tx.forward(tx.input);
    }
    if (!noteplug::letter(static_cast<unsigned char>(survivor))) return tx.forward(tx.input);
    bool was{};
    if (!eligible_at(tx.editor, before.doc, old_letter, was)) return tx.forward(tx.input);
    const auto joined_line = sci(tx.editor, SCI_LINEFROMPOSITION, static_cast<WPARAM>(first));
    const auto joined_start = sci(tx.editor, SCI_POSITIONFROMLINE, static_cast<WPARAM>(joined_line));
    if (joined_line < 0 || joined_start < 0 || joined_start > first) return tx.forward(tx.input);
    const auto removed = last-first;
    const auto shifted = old_letter-removed;
    std::string predicted_prefix;
    if (!read(tx.editor, before.doc, (std::max)(joined_start, first-2), first, predicted_prefix)) return tx.forward(tx.input);
    if (old_letter > last) predicted_prefix.push_back(' ');
    const bool will = eligible_prefix(predicted_prefix, shifted-joined_start);
    const char correction = noteplug::transition(survivor, was, will);
    if (correction == survivor || !unchanged(before, Input::deletion)) return tx.forward(tx.input);
    Undo undo(tx.editor);
    if (!unchanged(before, Input::deletion)) return tx.forward(tx.input);
    const auto result = tx.forward(tx.input);
    Context after{};
    char candidate{};
    bool actual_cap{};
    if (capture(tx.editor, Input::deletion, after) && after.doc == before.doc
        && after.len == before.len-removed && after.caret == first && after.anchor == first
        && byte_at(tx.editor, before.doc, shifted, candidate) && candidate == survivor
        && eligible_at(tx.editor, before.doc, shifted, actual_cap) && actual_cap == will) {
        (void)replace_letter(tx.editor, before.doc, shifted, survivor, correction, first);
    }
    return result;
}

LRESULT space(Tx& tx) {
    Context before{};
    if (!capture(tx.editor, Input::space, before)) return tx.forward(tx.input);
    std::intptr_t start=before.caret, end=before.caret;
    for (int n=0;n<2 && start>before.line_start;++n) {
        char c{}; if (!byte_at(tx.editor, before.doc, start-1, c)) return tx.forward(tx.input);
        if (c != ' ') break;
        --start;
    }
    for (int n=0;n<2 && end<before.line_end;++n) {
        char c{}; if (!byte_at(tx.editor, before.doc, end, c)) return tx.forward(tx.input);
        if (c != ' ') break;
        ++end;
    }
    const auto count = end-start;
    if (count == 1) {
        if (!unchanged(before, Input::space)) return tx.forward(tx.input);
        if (before.caret != start+1) sci(tx.editor, SCI_SETEMPTYSELECTION, static_cast<WPARAM>(start+1));
        return 0;
    }
    std::intptr_t original_letter_pos=end;
    char original_letter{};
    bool has_letter = original_letter_pos < before.line_end
        && byte_at(tx.editor, before.doc, original_letter_pos, original_letter)
        && noteplug::letter(static_cast<unsigned char>(original_letter));
    bool was = false;
    if (has_letter && !eligible_at(tx.editor, before.doc, original_letter_pos, was)) return tx.forward(tx.input);
    // With no adjacent spaces, avoid an unnecessary undo group unless the
    // following letter's eligibility is expected to change.
    if (count == 0) {
        if (has_letter) {
            std::string prefix;
            if (!read(tx.editor, before.doc, (std::max)(before.line_start, before.caret-2), before.caret, prefix))
                return tx.forward(tx.input);
            prefix.push_back(' ');
            const bool will = eligible_prefix(prefix, before.caret+1-before.line_start);
            if (will != was) {
                if (!unchanged(before, Input::space)) return tx.forward(tx.input);
                Undo undo(tx.editor);
                const auto result=tx.forward(tx.input);
                Context after{}; char c{}; bool now{};
                if (capture(tx.editor, Input::space, after) && after.doc == before.doc
                    && after.len == before.len+1 && after.caret == before.caret+1
                    && after.anchor == after.caret && byte_at(tx.editor, before.doc, after.caret, c)
                    && c == original_letter && eligible_at(tx.editor, before.doc, after.caret, now)) {
                    const auto replacement = noteplug::transition(c, was, now);
                    if (replacement != c) (void)replace_letter(tx.editor, before.doc, after.caret, c, replacement, after.caret);
                }
                return result;
            }
        }
        return tx.forward(tx.input);
    }
    std::string original;
    if (!read(tx.editor, before.doc, start, end, original) || !unchanged(before, Input::space))
        return tx.forward(tx.input);
    Undo undo(tx.editor);
    if (!unchanged(before, Input::space)) return tx.forward(tx.input);
    const auto new_caret = start+1;
    // A collapsed window is a replacement, not a second native Space dispatch.
    if (!replace(tx.editor, before.doc, start, end, original, " ", new_caret)) return 0;
    Context after{}; char next{}; bool now{};
    if (has_letter && capture(tx.editor, Input::space, after) && after.doc == before.doc
        && after.len == before.len-count+1 && after.caret == new_caret
        && byte_at(tx.editor, before.doc, new_caret, next) && next == original_letter
        && eligible_at(tx.editor, before.doc, new_caret, now)) {
        const auto correction = noteplug::transition(next, was, now);
        if (correction != next) (void)replace_letter(tx.editor, before.doc, new_caret, next, correction, new_caret);
    }
    return 0;
}

// Synchronous SCN_MODIFIED witness: only one native user insertion at the
// captured collapsed caret qualifies. It never reads or writes the clipboard.
struct Modification {
    NMHDR header;
    std::intptr_t position;
    int ch, modifiers, modification;
    const char* text;
    std::intptr_t length;
};
static_assert(offsetof(Modification, position) == 24 && offsetof(Modification, modification) == 40
    && offsetof(Modification, text) == 48 && offsetof(Modification, length) == 56);
struct PasteWitness { HWND editor{}; std::intptr_t start{}, length{}; unsigned inserts{}; bool native{}, bad{}; };
thread_local PasteWitness* paste_witness{};
void observe_modification(const Modification& event) noexcept {
    PasteWitness* w = paste_witness;
    if (!w || event.header.code != 2008 || event.header.hwndFrom != w->editor
        || !(event.modification & 3)) return;
    if (!w->native || (event.modification & 3) != 1 || !(event.modification & 0x10)
        || (event.modification & 0x60) || ++w->inserts != 1
        || event.position != w->start || event.length <= 0) w->bad = true;
    else w->length = event.length;
}
LRESULT paste(Tx& tx) {
    Context before{}; bool cap{};
    if (!capture(tx.editor, Input::paste, before)
        || !eligible_at(tx.editor, before.doc, before.caret, cap) || !cap)
        return tx.forward(tx.input);
    std::string left, right;
    if (!read(tx.editor, before.doc, (std::max)(before.line_start, before.caret-2), before.caret, left)
        || !read(tx.editor, before.doc, before.caret, (std::min)(before.len, before.caret+2), right)
        || !unchanged(before, Input::paste)) return tx.forward(tx.input);
    PasteWitness witness{tx.editor, before.caret};
    Undo undo(tx.editor);
    if (!unchanged(before, Input::paste)) return tx.forward(tx.input);
    paste_witness = &witness;
    witness.native = true;
    const auto result = tx.forward(tx.input);
    witness.native = false;
    paste_witness = nullptr;
    Context after{};
    if (witness.bad || witness.inserts != 1 || witness.length > (std::numeric_limits<std::intptr_t>::max)()-before.len
        || !capture(tx.editor, Input::paste, after) || after.doc != before.doc
        || after.len != before.len+witness.length
        || after.caret != before.caret+witness.length || after.anchor != after.caret) return result;
    std::string actual_left, actual_right;
    if (!read(tx.editor, after.doc, (std::max)(before.line_start, before.caret-2), before.caret, actual_left)
        || !read(tx.editor, after.doc, after.caret, (std::min)(after.len, after.caret+2), actual_right)
        || left != actual_left || right != actual_right) return result;
    char first{};
    if (byte_at(tx.editor, after.doc, before.caret, first) && noteplug::lower(static_cast<unsigned char>(first)))
        (void)replace_letter(tx.editor, after.doc, before.caret, first, noteplug::uppercase(first), after.caret);
    return result;
}

LRESULT enter(Tx& tx) {
    Context before{};
    if (!capture(tx.editor, Input::enter, before) || !boundary(tx.editor, before.caret)) return tx.forward(tx.input);
    std::string prefix;
    const auto from = (std::max)(before.line_start, before.caret-3);
    if (!read(tx.editor, before.doc, from, before.caret, prefix)) return tx.forward(tx.input);
    const auto trim = noteplug::enter_trim(prefix);
    char right{};
    const bool right_lower = before.caret < before.line_end
        && byte_at(tx.editor, before.doc, before.caret, right)
        && noteplug::lower(static_cast<unsigned char>(right));
    if (!trim && !right_lower) return tx.forward(tx.input);
    const auto original_lines = sci(tx.editor, SCI_GETLINECOUNT);
    const auto eol = sci(tx.editor, SCI_GETEOLMODE);
    if (original_lines < 1 || eol < 0 || eol > 2 || !unchanged(before, Input::enter)) return tx.forward(tx.input);
    Undo undo(tx.editor);
    if (!unchanged(before, Input::enter)) return tx.forward(tx.input);
    const auto result = tx.forward(tx.input);
    Context after{};
    if (!capture(tx.editor, Input::enter, after) || after.doc != before.doc
        || after.line != before.line+1 || sci(tx.editor, SCI_GETLINECOUNT) != original_lines+1
        || sci(tx.editor, SCI_GETEOLMODE) != eol
        || sci(tx.editor, SCI_GETLINEENDPOSITION, static_cast<WPARAM>(before.line)) != before.caret) return result;
    if (trim) {
        std::string old_spaces;
        if (!read(tx.editor, after.doc, before.caret-static_cast<std::intptr_t>(trim), before.caret, old_spaces)
            || old_spaces != std::string(trim, ' ')) return result;
        const auto expected_caret = after.caret-static_cast<std::intptr_t>(trim);
        if (expected_caret < 0 || expected_caret > after.len-static_cast<std::intptr_t>(trim)) return result;
        if (!replace(tx.editor, after.doc, before.caret-static_cast<std::intptr_t>(trim),
                     before.caret, old_spaces, "", expected_caret)) return result;
    }
    if (right_lower) {
        Context current{}; char current_letter{};
        if (capture(tx.editor, Input::enter, current) && current.doc == before.doc
            && current.caret == current.line_start && current.line == before.line+1
            && current.line_start < current.line_end
            && byte_at(tx.editor, current.doc, current.caret, current_letter)
            && current_letter == right)
            (void)replace_letter(tx.editor, current.doc, current.caret, right,
                                 noteplug::uppercase(right), current.caret);
    }
    return result;
}

LRESULT CALLBACK editor_proc(HWND editor, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) noexcept {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(editor, editor_proc, id);
        if (editor == host.first) editor_attached[0] = false;
        if (editor == host.second) editor_attached[1] = false;
    }
    if (busy) return DefSubclassProc(editor, message, w, l);
    if (message == WM_CHAR && ((w <= 127 && noteplug::letter(static_cast<unsigned char>(w))) || w == ' ')) {
        Tx tx(editor,editor,message,w,l);
        return w == ' ' ? space(tx) : typed(tx);
    }
    if (message == WM_KEYDOWN && (w == VK_DELETE || w == VK_BACK || w == VK_RETURN)) {
        Tx tx(editor,editor,message,w,l);
        return w == VK_RETURN ? enter(tx) : deletion(tx);
    }
    if ((((message == sci_paste || message == WM_PASTE) && !w) ||
        (message == WM_COMMAND && w == 14)) && !l) {
        Tx tx(editor,editor,message,w,l); return paste(tx);
    }
    return DefSubclassProc(editor, message, w, l);
}
LRESULT CALLBACK main_proc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) noexcept {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, main_proc, id);
        main_attached = false;
    }
    if (!busy && message == WM_COMMAND && LOWORD(w) == npp_paste && !l) {
        HWND editor = GetFocus();
        if (editor == host.first || editor == host.second) {
            Tx tx(editor,window,message,w,l); return paste(tx);
        }
    }
    return DefSubclassProc(window, message, w, l);
}
void detach() noexcept {
    for (int i=0;i<2;++i) {
        HWND editor = i == 0 ? host.first : host.second;
        if (editor_attached[i] && editor && GetWindowThreadProcessId(editor,nullptr) == GetCurrentThreadId())
            RemoveWindowSubclass(editor, editor_proc, subclass_id);
        editor_attached[i] = false;
    }
    if (main_attached && host.main && GetWindowThreadProcessId(host.main,nullptr) == GetCurrentThreadId())
        RemoveWindowSubclass(host.main, main_proc, subclass_id);
    main_attached = false;
    paste_witness = nullptr;
}
void attach() noexcept {
    if (!host.main || GetWindowThreadProcessId(host.main,nullptr) != GetCurrentThreadId()) return;
    HWND editors[]{host.first,host.second};
    for (int i=0;i<2;++i) {
        if (editors[i] && IsChild(host.main,editors[i])
            && GetWindowThreadProcessId(editors[i],nullptr) == GetCurrentThreadId())
            editor_attached[i] = SetWindowSubclass(editors[i],editor_proc,subclass_id,0) != FALSE;
    }
    main_attached = SetWindowSubclass(host.main,main_proc,subclass_id,0) != FALSE;
    if (!editor_attached[0] || !editor_attached[1] || !main_attached) detach();
}
} // namespace

extern "C" __declspec(dllexport) BOOL isUnicode() noexcept { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() noexcept { return L"CapitalizeNext"; }
extern "C" __declspec(dllexport) void setInfo(NppData data) noexcept { detach(); host = data; }
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) noexcept {
    static FuncItem separator{};
    if (count) *count = 1;
    return &separator;
}
extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) noexcept {
    if (!notification) return;
    const auto* header = reinterpret_cast<const NMHDR*>(notification);
    if (header->code == 2008) observe_modification(*reinterpret_cast<const Modification*>(notification));
    if (header->code == npp_ready) attach();
    else if (header->code == npp_shutdown) detach();
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) noexcept { return TRUE; }
