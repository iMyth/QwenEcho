/**
 * Unit tests for the NativePort module.
 *
 * Uses a mock Dart_PostCObject function to capture dispatched messages
 * and verify their structure matches the design specification.
 */

#include "native_port.h"
#include "echo_types.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Mock infrastructure
// ---------------------------------------------------------------------------

namespace {

/** Captured message for verification */
struct CapturedMessage {
    Dart_Port port_id;
    Dart_CObject_Type type;
    int array_length;
    // Simplified capture of first few elements for verification
    std::vector<Dart_CObject_Type> element_types;
    std::vector<int32_t> int32_values;
    std::vector<int64_t> int64_values;
    std::vector<double> double_values;
    std::vector<std::string> string_values;
};

static std::vector<CapturedMessage> g_captured_messages;

/** Mock post function that captures the message structure */
bool mock_post_cobject(Dart_Port port_id, Dart_CObject* message)
{
    CapturedMessage captured;
    captured.port_id = port_id;
    captured.type = message->type;

    if (message->type == Dart_CObject_kArray) {
        captured.array_length = message->value.as_array.length;
        for (int i = 0; i < captured.array_length; i++) {
            Dart_CObject* elem = message->value.as_array.values[i];
            captured.element_types.push_back(elem->type);
            switch (elem->type) {
                case Dart_CObject_kInt32:
                    captured.int32_values.push_back(elem->value.as_int32);
                    captured.int64_values.push_back(0);
                    captured.double_values.push_back(0.0);
                    captured.string_values.push_back("");
                    break;
                case Dart_CObject_kInt64:
                    captured.int32_values.push_back(0);
                    captured.int64_values.push_back(elem->value.as_int64);
                    captured.double_values.push_back(0.0);
                    captured.string_values.push_back("");
                    break;
                case Dart_CObject_kDouble:
                    captured.int32_values.push_back(0);
                    captured.int64_values.push_back(0);
                    captured.double_values.push_back(elem->value.as_double);
                    captured.string_values.push_back("");
                    break;
                case Dart_CObject_kString:
                    captured.int32_values.push_back(0);
                    captured.int64_values.push_back(0);
                    captured.double_values.push_back(0.0);
                    captured.string_values.push_back(elem->value.as_string ? elem->value.as_string : "");
                    break;
                default:
                    captured.int32_values.push_back(0);
                    captured.int64_values.push_back(0);
                    captured.double_values.push_back(0.0);
                    captured.string_values.push_back("");
                    break;
            }
        }
    }

    g_captured_messages.push_back(captured);
    return true;
}

void reset_mock()
{
    g_captured_messages.clear();
}

void setup_port()
{
    native_port_set_post_fn(mock_post_cobject);
    native_port_register(42);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_port_not_registered_returns_false()
{
    printf("  test_port_not_registered_returns_false... ");

    // Create a fresh state by NOT registering - but since native_port uses
    // global state and we already registered, we test the "no post fn" case
    // by setting post_fn to nullptr temporarily.
    native_port_set_post_fn(nullptr);
    native_port_register(42);

    bool result = native_port_post_asr_partial(0, "hello", 1000);
    assert(!result);

    // Restore for subsequent tests
    native_port_set_post_fn(mock_post_cobject);
    printf("PASS\n");
}

void test_port_registration_replacement()
{
    printf("  test_port_registration_replacement... ");
    reset_mock();
    setup_port();

    // Register first port
    native_port_register(100);
    native_port_post_asr_partial(0, "test1", 1000);
    assert(g_captured_messages.back().port_id == 100);

    // Register second port (replaces first)
    native_port_register(200);
    native_port_post_asr_partial(0, "test2", 2000);
    assert(g_captured_messages.back().port_id == 200);

    printf("PASS\n");
}

void test_is_registered()
{
    printf("  test_is_registered... ");

    // After setup_port, should be registered
    setup_port();
    assert(native_port_is_registered());

    printf("PASS\n");
}

void test_asr_partial_message_format()
{
    printf("  test_asr_partial_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_asr_partial(1, "hello world", 12345);
    assert(ok);
    assert(g_captured_messages.size() == 1);

    auto& msg = g_captured_messages[0];
    assert(msg.type == Dart_CObject_kArray);
    assert(msg.array_length == 4);  // [type, speaker_id, text, timestamp_ms]

    // Element 0: type tag (MSG_ASR_PARTIAL = 1)
    assert(msg.element_types[0] == Dart_CObject_kInt32);
    assert(msg.int32_values[0] == MSG_ASR_PARTIAL);

    // Element 1: speaker_id
    assert(msg.element_types[1] == Dart_CObject_kInt32);
    assert(msg.int32_values[1] == 1);

    // Element 2: text
    assert(msg.element_types[2] == Dart_CObject_kString);
    assert(msg.string_values[2] == "hello world");

    // Element 3: timestamp_ms
    assert(msg.element_types[3] == Dart_CObject_kInt64);
    assert(msg.int64_values[3] == 12345);

    printf("PASS\n");
}

void test_asr_confirmed_message_format()
{
    printf("  test_asr_confirmed_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_asr_confirmed(0, "confirmed text", 99999, 7);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 5);  // [type, speaker_id, text, timestamp_ms, segment_id]
    assert(msg.int32_values[0] == MSG_ASR_CONFIRMED);
    assert(msg.int32_values[1] == 0);
    assert(msg.string_values[2] == "confirmed text");
    assert(msg.int64_values[3] == 99999);
    assert(msg.int32_values[4] == 7);

    printf("PASS\n");
}

void test_translation_stream_message_format()
{
    printf("  test_translation_stream_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_translation_stream(1, "token", 42);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 4);  // [type, speaker_id, token, segment_id]
    assert(msg.int32_values[0] == MSG_TRANSLATION_STREAM);
    assert(msg.int32_values[1] == 1);
    assert(msg.string_values[2] == "token");
    assert(msg.int32_values[3] == 42);

    printf("PASS\n");
}

void test_translation_done_message_format()
{
    printf("  test_translation_done_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_translation_done(0, "full translation", 10);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 4);  // [type, speaker_id, full_text, segment_id]
    assert(msg.int32_values[0] == MSG_TRANSLATION_DONE);
    assert(msg.int32_values[1] == 0);
    assert(msg.string_values[2] == "full translation");
    assert(msg.int32_values[3] == 10);

    printf("PASS\n");
}

void test_tts_started_message_format()
{
    printf("  test_tts_started_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_tts_started(1, 55);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 3);  // [type, speaker_id, segment_id]
    assert(msg.int32_values[0] == MSG_TTS_STARTED);
    assert(msg.int32_values[1] == 1);
    assert(msg.int32_values[2] == 55);

    printf("PASS\n");
}

void test_tts_complete_message_format()
{
    printf("  test_tts_complete_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_tts_complete(0, 55);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 3);  // [type, speaker_id, segment_id]
    assert(msg.int32_values[0] == MSG_TTS_COMPLETE);
    assert(msg.int32_values[1] == 0);
    assert(msg.int32_values[2] == 55);

    printf("PASS\n");
}

void test_error_message_format()
{
    printf("  test_error_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_error(-3, "asr_model", "file not found");
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 4);  // [type, error_code, model_name, detail]
    assert(msg.int32_values[0] == MSG_ERROR);
    assert(msg.int32_values[1] == -3);
    assert(msg.string_values[2] == "asr_model");
    assert(msg.string_values[3] == "file not found");

    printf("PASS\n");
}

void test_thermal_state_message_format()
{
    printf("  test_thermal_state_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_thermal_state(1, 43.5);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 3);  // [type, thermal_mode, temperature_c]
    assert(msg.int32_values[0] == MSG_THERMAL_STATE);
    assert(msg.int32_values[1] == 1);
    assert(msg.double_values[2] == 43.5);

    printf("PASS\n");
}

void test_memory_warning_message_format()
{
    printf("  test_memory_warning_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_memory_warning(2000000000LL, 2500000000LL, 1);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 4);  // [type, current_bytes, limit_bytes, level]
    assert(msg.int32_values[0] == MSG_MEMORY_WARNING);
    assert(msg.int64_values[1] == 2000000000LL);
    assert(msg.int64_values[2] == 2500000000LL);
    assert(msg.int32_values[3] == 1);

    printf("PASS\n");
}

void test_latency_warning_message_format()
{
    printf("  test_latency_warning_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_latency_warning("ASR", 250, 200);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 4);  // [type, stage, actual_ms, budget_ms]
    assert(msg.int32_values[0] == MSG_LATENCY_WARNING);
    assert(msg.string_values[1] == "ASR");
    assert(msg.int32_values[2] == 250);
    assert(msg.int32_values[3] == 200);

    printf("PASS\n");
}

void test_sample_drop_message_format()
{
    printf("  test_sample_drop_message_format... ");
    reset_mock();
    setup_port();

    bool ok = native_port_post_sample_drop(320, 54321);
    assert(ok);

    auto& msg = g_captured_messages[0];
    assert(msg.array_length == 3);  // [type, dropped_samples, timestamp_ms]
    assert(msg.int32_values[0] == MSG_SAMPLE_DROP);
    assert(msg.int32_values[1] == 320);
    assert(msg.int64_values[2] == 54321);

    printf("PASS\n");
}

void test_port_id_sent_correctly()
{
    printf("  test_port_id_sent_correctly... ");
    reset_mock();
    native_port_set_post_fn(mock_post_cobject);
    native_port_register(12345);

    native_port_post_tts_started(0, 1);
    assert(g_captured_messages[0].port_id == 12345);

    printf("PASS\n");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("Running NativePort tests...\n");

    test_port_not_registered_returns_false();
    test_port_registration_replacement();
    test_is_registered();
    test_asr_partial_message_format();
    test_asr_confirmed_message_format();
    test_translation_stream_message_format();
    test_translation_done_message_format();
    test_tts_started_message_format();
    test_tts_complete_message_format();
    test_error_message_format();
    test_thermal_state_message_format();
    test_memory_warning_message_format();
    test_latency_warning_message_format();
    test_sample_drop_message_format();
    test_port_id_sent_correctly();

    printf("\nAll NativePort tests PASSED!\n");
    return 0;
}
