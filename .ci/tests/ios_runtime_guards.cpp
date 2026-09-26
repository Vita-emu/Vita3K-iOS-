#include <vita3k_ios/TextInput.h>
#include <renderer/cache_validation.h>
#include <cassert>
#include <limits>
#include <mutex>
#include <vector>

constexpr int IME_DIALOG = 1, SCE_COMMON_DIALOG_STATUS_RUNNING = 1, SCE_COMMON_DIALOG_STATUS_FINISHED = 2;
constexpr int SCE_IME_EVENT_OPEN = 0, SCE_IME_EVENT_PRESS_ENTER = 1, SCE_IME_EVENT_PRESS_CLOSE = 2, SCE_IME_EVENT_UPDATE_TEXT = 3;
constexpr int SCE_IME_DIALOG_BUTTON_CLOSE = 1, SCE_IME_DIALOG_BUTTON_ENTER = 2;
constexpr int SCE_COMMON_DIALOG_RESULT_USER_CANCELED = 1, SCE_COMMON_DIALOG_RESULT_OK = 0, SCE_IME_MAX_TEXT_LENGTH = 2048;
struct FakeEditText { uint32_t preeditIndex, preeditLength, caretIndex, str, editIndex; int32_t editLengthChange; };
struct FakeIme {
    std::mutex mutex;
    uint64_t session_id = 1;
    bool state = false;
    int event_id = SCE_IME_EVENT_OPEN;
    std::u16string str;
    uint32_t caretIndex = 0;
    struct { uint32_t maxTextLength = 10; } param;
    FakeEditText edit_text{};
};
struct FakeDialog {
    std::recursive_mutex mutex;
    int type = 0, status = 0, result = 0;
    struct {
        uint32_t max_length = 10;
        bool cancelable = true, multiline = false;
        const char *title = "Name";
        uint16_t *result = nullptr;
        int status = 0;
    } ime;
};
struct EmuEnvState { FakeIme ime; FakeDialog common_dialog; };
std::optional<Vita3KIOSTextResult> reply;
std::optional<Vita3KIOSTextRequest> visible;
std::optional<Vita3KIOSTextResult> vita3k_ios_take_text_result() { auto r = reply; reply.reset(); return r; }
void vita3k_ios_update_text_input(const std::optional<Vita3KIOSTextRequest> &request) { visible = request; }
// INSERT_COORDINATOR
constexpr int SCE_IME_EVENT_UPDATE_CARET = 4;
struct FakeEvent { uint32_t id; union { FakeEditText text; uint32_t caretIndex; } param; };
void event_payload(EmuEnvState &emuenv, FakeEvent *e) {
// INSERT_EVENT_PAYLOAD
}

int main() {
    std::u16string unicode = u"A\U0001F600B";
    vita3k_ios_limit_text(unicode, 2);
    assert(unicode == u"A");
    unicode = u"A\U0001F600B";
    vita3k_ios_limit_text(unicode, 3);
    assert(unicode == u"A\U0001F600");
    vita3k_ios_limit_text(unicode, 0);
    assert(unicode.empty());

    EmuEnvState env;
    IOSInputSession session;
    env.ime.edit_text.preeditIndex = 1;
    env.ime.caretIndex = 3;
    FakeEvent edit_event{};
    edit_event.id = SCE_IME_EVENT_UPDATE_TEXT;
    event_payload(env, &edit_event);
    assert(edit_event.param.text.preeditIndex == 1);
    edit_event.id = SCE_IME_EVENT_UPDATE_CARET;
    event_payload(env, &edit_event);
    assert(edit_event.param.caretIndex == 3);
    uint16_t output[12]{};
    output[11] = 0xbeef;
    env.common_dialog.type = IME_DIALOG;
    env.common_dialog.status = SCE_COMMON_DIALOG_STATUS_RUNNING;
    env.common_dialog.ime.result = output;
    session.update(env);
    assert(visible && visible->id == 1);
    reply = Vita3KIOSTextResult{999, u"stale", false};
    session.update(env);
    assert(env.common_dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING);
    reply = Vita3KIOSTextResult{1, u"123456789012345", false};
    session.update(env);
    assert(env.common_dialog.status == SCE_COMMON_DIALOG_STATUS_FINISHED);
    assert(output[9] == u'0' && output[10] == 0 && output[11] == 0xbeef);
    assert(!visible);

    ++env.ime.session_id;
    env.common_dialog.status = SCE_COMMON_DIALOG_STATUS_RUNNING;
    env.common_dialog.ime.cancelable = false;
    reply = Vita3KIOSTextResult{2, u"", true};
    session.update(env);
    assert(env.common_dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING && visible);
    env.common_dialog.ime.cancelable = true;
    reply = Vita3KIOSTextResult{2, u"", true};
    session.update(env);
    assert(env.common_dialog.result == SCE_COMMON_DIALOG_RESULT_USER_CANCELED);

    ++env.ime.session_id;
    env.common_dialog.type = 0;
    env.ime.state = true;
    reply = Vita3KIOSTextResult{3, u"Hero", false};
    session.update(env);
    assert(env.ime.str == u"Hero" && env.ime.event_id == SCE_IME_EVENT_UPDATE_TEXT);
    session.update(env);
    assert(env.ime.event_id == SCE_IME_EVENT_UPDATE_TEXT); // guest has not consumed it
    env.ime.event_id = SCE_IME_EVENT_OPEN;
    session.update(env);
    assert(env.ime.event_id == SCE_IME_EVENT_PRESS_ENTER);
    env.ime.state = false;
    reply = Vita3KIOSTextResult{3, u"late", false};
    session.update(env);
    assert(env.ime.str == u"Hero" && !visible);

    std::vector<uint32_t> spirv{0x07230203, 0x10000, 0, 1, 0, (2u << 16) | 17u, 1};
    assert(valid_spirv_cache(spirv));
    spirv.pop_back();
    assert(!valid_spirv_cache(spirv));
    spirv[5] = 0;
    assert(!valid_spirv_cache(spirv));
    spirv[0] = 0;
    assert(!valid_spirv_cache(spirv));
    assert(!valid_spirv_cache({}));
    assert(valid_pipeline_hash_count(100, 10));
    assert(!valid_pipeline_hash_count(5, 0));
    assert(!valid_pipeline_hash_count(100, std::numeric_limits<size_t>::max()));
}
