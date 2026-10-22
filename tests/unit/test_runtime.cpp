/**
 * @file tests/unit/test_runtime.cpp
 * @brief Unit tests for runtime and virtual gamepad handles.
 */

// lib includes
#include <libvirtualhid/libvirtualhid.hpp>

// standard includes
#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "fixtures/fixtures.hpp"
#if defined(_WIN32)
  #include "platform/windows/shared/lvh_windows_protocol.h"
#endif

/**
 * @brief Test fixture for Linux runtime integration tests.
 */
class LinuxRuntimeTest: public LinuxTest {};

TEST(RuntimeTest, FakeBackendReportsCapabilities) {
  auto runtime = lvh::Runtime::create();

  EXPECT_EQ(runtime->backend_kind(), lvh::BackendKind::fake);
  EXPECT_EQ(runtime->capabilities().backend_name, "fake");
  EXPECT_TRUE(runtime->capabilities().supports_gamepad);
  EXPECT_TRUE(runtime->capabilities().supports_keyboard);
  EXPECT_TRUE(runtime->capabilities().supports_mouse);
  EXPECT_TRUE(runtime->capabilities().supports_touchscreen);
  EXPECT_TRUE(runtime->capabilities().supports_trackpad);
  EXPECT_TRUE(runtime->capabilities().supports_pen_tablet);
  EXPECT_TRUE(runtime->capabilities().supports_output_reports);
  EXPECT_FALSE(runtime->capabilities().requires_installed_driver);
}

TEST(RuntimeTest, RoutesDiagnosticsToConsumerCallback) {
  struct Diagnostic {
    lvh::LogLevel level;
    std::string message;
  };

  std::vector<Diagnostic> diagnostics;

  lvh::RuntimeOptions runtime_options;
  runtime_options.log_callback = [&diagnostics](lvh::LogLevel level, const std::string &message) {
    diagnostics.emplace_back(level, message);
  };
  auto runtime = lvh::Runtime::create(runtime_options);

  lvh::CreateMouseOptions mouse_options;
  mouse_options.profile = lvh::profiles::mouse();
  mouse_options.desktop = {.offset_x = -1920, .offset_y = 0, .width = 3840, .height = 1080};
  mouse_options.viewport = {.offset_x = -1920, .offset_y = 0, .width = 1920, .height = 1080};
  auto created = runtime->create_mouse(mouse_options);
  ASSERT_TRUE(created) << created.status.message();
  EXPECT_TRUE(created.mouse->move_relative(1, -1).ok());
  EXPECT_TRUE(created.mouse->move_absolute(960, 540, 1920, 1080).ok());
  EXPECT_TRUE(created.mouse->move_absolute(0.5F, 0.25F, 1, 1).ok());
  EXPECT_TRUE(created.mouse->button(lvh::MouseButton::left, true).ok());
  EXPECT_TRUE(created.mouse->vertical_scroll(120).ok());
  EXPECT_TRUE(created.mouse->horizontal_scroll(-120).ok());
  EXPECT_TRUE(created.mouse->close().ok());
  EXPECT_EQ(created.mouse->move_relative(1, 1).code(), lvh::ErrorCode::device_closed);

  const auto has_message = [&diagnostics](lvh::LogLevel level, std::string_view text) {
    return std::ranges::any_of(diagnostics, [level, text](const auto &diagnostic) {
      return diagnostic.level == level && diagnostic.message.contains(text);
    });
  };
  EXPECT_TRUE(has_message(lvh::LogLevel::info, "initialized fake backend"));
  EXPECT_TRUE(has_message(lvh::LogLevel::info, "created mouse 1"));
  EXPECT_TRUE(has_message(lvh::LogLevel::debug, "viewport=-1920,0 1920x1080"));
  EXPECT_TRUE(has_message(lvh::LogLevel::error, "mouse input failed: mouse is closed"));
}

TEST(RuntimeTest, DisablesConsumerDiagnosticsAfterException) {
  struct ConsumerLoggerFailure {};

  std::size_t callback_count = 0;
  lvh::RuntimeOptions options;
  options.log_callback = [&callback_count](lvh::LogLevel, const std::string &) {
    ++callback_count;
    throw ConsumerLoggerFailure {};
  };

  EXPECT_NO_THROW({
    auto runtime = lvh::Runtime::create(options);
    auto created = runtime->create_mouse();
    ASSERT_TRUE(created);
    EXPECT_TRUE(created.mouse->move_relative(1, 1).ok());
  });
  EXPECT_EQ(callback_count, 1U);
}

TEST(RuntimeTest, PlatformDefaultReportsCurrentPlatformCapabilities) {
  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);

  EXPECT_EQ(runtime->backend_kind(), lvh::BackendKind::platform_default);

#if defined(__linux__)
  EXPECT_EQ(runtime->capabilities().backend_name, "linux-uhid-uinput");
  EXPECT_FALSE(runtime->capabilities().requires_installed_driver);
#elif defined(__FreeBSD__)
  EXPECT_EQ(runtime->capabilities().backend_name, "freebsd-uinput");
  EXPECT_FALSE(runtime->capabilities().requires_installed_driver);
  EXPECT_TRUE(runtime->capabilities().supports_gamepad);

  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
  ASSERT_TRUE(created) << created.status.message();
  EXPECT_TRUE(created.gamepad->close().ok());
#elif defined(_WIN32)
  EXPECT_EQ(runtime->capabilities().backend_name, "windows-umdf");
  EXPECT_TRUE(runtime->capabilities().requires_installed_driver);
  EXPECT_TRUE(runtime->capabilities().supports_keyboard);
  EXPECT_TRUE(runtime->capabilities().supports_mouse);
  EXPECT_FALSE(runtime->capabilities().supports_trackpad);

  auto keyboard = runtime->create_keyboard();
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  EXPECT_TRUE(keyboard.keyboard->close().ok());
  auto mouse = runtime->create_mouse();
  ASSERT_TRUE(mouse) << mouse.status.message();
  EXPECT_TRUE(mouse.mouse->close().ok());

  auto touchscreen = runtime->create_touchscreen();
  if (runtime->capabilities().supports_touchscreen) {
    ASSERT_TRUE(touchscreen) << touchscreen.status.message();
    EXPECT_TRUE(touchscreen.touchscreen->close().ok());
  } else {
    EXPECT_FALSE(touchscreen);
  }

  EXPECT_EQ(runtime->create_trackpad().status.code(), lvh::ErrorCode::unsupported_profile);

  auto pen_tablet = runtime->create_pen_tablet();
  if (runtime->capabilities().supports_pen_tablet) {
    ASSERT_TRUE(pen_tablet) << pen_tablet.status.message();
    EXPECT_TRUE(pen_tablet.pen_tablet->close().ok());
  } else {
    EXPECT_FALSE(pen_tablet);
  }

  if (!runtime->capabilities().supports_gamepad) {
    auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
    EXPECT_FALSE(created);
    EXPECT_EQ(created.status.code(), lvh::ErrorCode::backend_unavailable);
  } else {
    auto xbox_360 = runtime->create_gamepad(lvh::profiles::xbox_360());
    ASSERT_TRUE(xbox_360) << xbox_360.status.message();
    ASSERT_NE(xbox_360.gamepad, nullptr);
    EXPECT_FALSE(xbox_360.gamepad->device_nodes().empty());
    EXPECT_TRUE(xbox_360.gamepad->close().ok());

    auto created = runtime->create_gamepad(lvh::profiles::xbox_series());
    ASSERT_TRUE(created) << created.status.message();
    ASSERT_NE(created.gamepad, nullptr);
    EXPECT_FALSE(created.gamepad->device_nodes().empty());

    lvh::GamepadState state;
    state.buttons.set(lvh::GamepadButton::a);
    state.left_stick.x = 0.25F;
    state.right_trigger = 1.0F;

    EXPECT_TRUE(created.gamepad->submit(state).ok());
    EXPECT_TRUE(created.gamepad->close().ok());
    EXPECT_TRUE(created.gamepad->close().ok());
    EXPECT_EQ(created.gamepad->submit(state).code(), lvh::ErrorCode::device_closed);
  }

  auto invalid_profile = lvh::profiles::xbox_series();
  invalid_profile.report_descriptor.resize(LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE + 1U);
  EXPECT_EQ(runtime->create_gamepad(invalid_profile).status.code(), lvh::ErrorCode::invalid_argument);

  invalid_profile = lvh::profiles::xbox_series();
  invalid_profile.input_report_size = LVH_WINDOWS_MAX_INPUT_REPORT_SIZE + 1U;
  EXPECT_EQ(runtime->create_gamepad(invalid_profile).status.code(), lvh::ErrorCode::invalid_argument);

  invalid_profile = lvh::profiles::xbox_series();
  invalid_profile.output_report_size = LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE + 1U;
  EXPECT_EQ(runtime->create_gamepad(invalid_profile).status.code(), lvh::ErrorCode::invalid_argument);
#elif defined(__APPLE__) && defined(__MACH__)
  EXPECT_EQ(runtime->capabilities().backend_name, "macos-virtual-hid-coregraphics");
  EXPECT_FALSE(runtime->capabilities().requires_installed_driver);
  EXPECT_TRUE(runtime->capabilities().supports_virtual_hid);
  EXPECT_TRUE(runtime->capabilities().supports_gamepad);
  EXPECT_TRUE(runtime->capabilities().supports_keyboard);
  EXPECT_TRUE(runtime->capabilities().supports_mouse);
  EXPECT_FALSE(runtime->capabilities().supports_touchscreen);
  EXPECT_FALSE(runtime->capabilities().supports_trackpad);
  EXPECT_FALSE(runtime->capabilities().supports_pen_tablet);
  EXPECT_TRUE(runtime->capabilities().supports_output_reports);

  auto keyboard = runtime->create_keyboard();
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  EXPECT_TRUE(keyboard.keyboard->type_text({.text = "A"}).ok());
  EXPECT_TRUE(keyboard.keyboard->close().ok());

  auto mouse = runtime->create_mouse();
  ASSERT_TRUE(mouse) << mouse.status.message();
  EXPECT_TRUE(mouse.mouse->close().ok());

  EXPECT_EQ(runtime->create_touchscreen().status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(runtime->create_trackpad().status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(runtime->create_pen_tablet().status.code(), lvh::ErrorCode::unsupported_profile);
#else
  EXPECT_EQ(runtime->capabilities().backend_name, "platform-default-unimplemented");
  EXPECT_FALSE(runtime->capabilities().supports_gamepad);

  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
  EXPECT_FALSE(created);
  EXPECT_EQ(created.status.code(), lvh::ErrorCode::backend_unavailable);
#endif
}

TEST(RuntimeTest, CreatesSubmitsAndClosesGamepad) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());

  ASSERT_TRUE(created);
  ASSERT_NE(created.gamepad, nullptr);
  EXPECT_TRUE(created.gamepad->is_open());
  EXPECT_TRUE(created.gamepad->device_nodes().empty());
  EXPECT_EQ(runtime->active_device_count(), 1U);

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::b);
  state.left_stick.x = 2.0F;
  state.left_trigger = 2.0F;

  EXPECT_TRUE(created.gamepad->submit(state).ok());
  EXPECT_EQ(created.gamepad->submit_count(), 1U);
  EXPECT_EQ(created.gamepad->last_submitted_state().left_stick.x, 1.0F);
  EXPECT_EQ(created.gamepad->last_submitted_state().left_trigger, 1.0F);
  EXPECT_FALSE(created.gamepad->last_input_report().empty());

  EXPECT_TRUE(created.gamepad->close().ok());
  EXPECT_FALSE(created.gamepad->is_open());
  EXPECT_EQ(runtime->active_device_count(), 0U);
  EXPECT_EQ(created.gamepad->submit(state).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, DispatchesOutputCallback) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_gamepad(lvh::profiles::dualsense());
  ASSERT_TRUE(created);

  lvh::GamepadOutput received;
  bool was_called = false;
  created.gamepad->set_output_callback([&received, &was_called](const lvh::GamepadOutput &output) {
    received = output;
    was_called = true;
  });

  lvh::GamepadOutput output;
  output.kind = lvh::GamepadOutputKind::rumble;
  output.low_frequency_rumble = 123;
  output.high_frequency_rumble = 456;

  EXPECT_TRUE(created.gamepad->dispatch_output(output).ok());
  EXPECT_TRUE(was_called);
  EXPECT_EQ(received.kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(received.low_frequency_rumble, 123);
  EXPECT_EQ(received.high_frequency_rumble, 456);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesKeyboard) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_keyboard();

  ASSERT_TRUE(created);
  ASSERT_NE(created.keyboard, nullptr);
  EXPECT_TRUE(created.keyboard->is_open());
  EXPECT_TRUE(created.keyboard->device_nodes().empty());
  EXPECT_EQ(created.keyboard->profile().device_type, lvh::DeviceType::keyboard);
  EXPECT_EQ(runtime->active_device_count(), 1U);

  EXPECT_TRUE(created.keyboard->press(0x41).ok());
  EXPECT_EQ(created.keyboard->submit_count(), 1U);
  EXPECT_EQ(created.keyboard->last_submitted_event().key_code, 0x41);
  EXPECT_TRUE(created.keyboard->last_submitted_event().pressed);

  EXPECT_TRUE(created.keyboard->release(0x41).ok());
  EXPECT_TRUE(created.keyboard->type_text({.text = "A"}).ok());
  EXPECT_EQ(created.keyboard->submit_count(), 3U);

  EXPECT_EQ(created.keyboard->press(0).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_TRUE(created.keyboard->close().ok());
  EXPECT_FALSE(created.keyboard->is_open());
  EXPECT_EQ(runtime->active_device_count(), 0U);
  EXPECT_EQ(created.keyboard->press(0x41).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesMouse) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_mouse();

  ASSERT_TRUE(created);
  ASSERT_NE(created.mouse, nullptr);
  EXPECT_TRUE(created.mouse->is_open());
  EXPECT_TRUE(created.mouse->device_nodes().empty());
  EXPECT_EQ(created.mouse->profile().device_type, lvh::DeviceType::mouse);
  EXPECT_EQ(runtime->active_device_count(), 1U);

  EXPECT_TRUE(created.mouse->move_relative(10, -5).ok());
  EXPECT_EQ(created.mouse->submit_count(), 1U);
  EXPECT_EQ(created.mouse->last_submitted_event().kind, lvh::MouseEventKind::relative_motion);
  EXPECT_EQ(created.mouse->last_submitted_event().x, 10);
  EXPECT_EQ(created.mouse->last_submitted_event().y, -5);

  EXPECT_TRUE(created.mouse->move_absolute(100, 200, 1920, 1080).ok());
  EXPECT_EQ(created.mouse->last_submitted_event().kind, lvh::MouseEventKind::absolute_motion);
  EXPECT_EQ(created.mouse->last_submitted_event().x, 100);
  EXPECT_EQ(created.mouse->last_submitted_event().y, 200);
  EXPECT_EQ(created.mouse->last_submitted_event().width, 1920);
  EXPECT_EQ(created.mouse->last_submitted_event().height, 1080);

  EXPECT_TRUE(created.mouse->move_absolute(0.25F, 0.75F, 1, 1).ok());
  EXPECT_EQ(created.mouse->last_submitted_event().kind, lvh::MouseEventKind::absolute_motion);
  EXPECT_TRUE(created.mouse->last_submitted_event().has_fractional_absolute_coordinates);
  EXPECT_FLOAT_EQ(created.mouse->last_submitted_event().absolute_x, 0.25F);
  EXPECT_FLOAT_EQ(created.mouse->last_submitted_event().absolute_y, 0.75F);
  EXPECT_TRUE(created.mouse->button(lvh::MouseButton::right, true).ok());
  EXPECT_TRUE(created.mouse->vertical_scroll(120).ok());
  EXPECT_TRUE(created.mouse->horizontal_scroll(-120).ok());
  EXPECT_EQ(created.mouse->submit_count(), 6U);

  EXPECT_EQ(created.mouse->move_absolute(1, 1, 0, 0).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_TRUE(created.mouse->close().ok());
  EXPECT_FALSE(created.mouse->is_open());
  EXPECT_EQ(runtime->active_device_count(), 0U);
  EXPECT_EQ(created.mouse->move_relative(1, 1).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, RejectsIncompleteMouseViewport) {
  auto runtime = lvh::Runtime::create();
  const auto expect_invalid = [&runtime](lvh::PointerViewport desktop, lvh::PointerViewport viewport) {
    lvh::CreateMouseOptions options;
    options.profile = lvh::profiles::mouse();
    options.desktop = desktop;
    options.viewport = viewport;

    const auto created = runtime->create_mouse(options);
    EXPECT_FALSE(created);
    EXPECT_EQ(created.status.code(), lvh::ErrorCode::invalid_argument);
  };

  expect_invalid({}, {.width = 1920});
  expect_invalid({}, {.width = 1920, .height = 1080});
  expect_invalid({.width = 3840, .height = 1080}, {});
  expect_invalid({.width = -1, .height = 1080}, {});
  expect_invalid(
    {.offset_x = -1920, .width = 3840, .height = 1080},
    {.offset_x = 0, .offset_y = 1080, .width = 1920, .height = 1080}
  );
}

TEST(RuntimeTest, CreatesSubmitsAndClosesTouchDevices) {
  auto runtime = lvh::Runtime::create();

  auto touchscreen = runtime->create_touchscreen();
  ASSERT_TRUE(touchscreen);
  ASSERT_NE(touchscreen.touchscreen, nullptr);
  EXPECT_EQ(touchscreen.touchscreen->profile().device_type, lvh::DeviceType::touchscreen);

  lvh::TouchContact contact {
    .id = 1,
    .x = 0.5F,
    .y = 0.25F,
    .pressure = 1.0F,
    .orientation = 10,
  };
  EXPECT_TRUE(touchscreen.touchscreen->place_contact(contact).ok());
  EXPECT_TRUE(touchscreen.touchscreen->release_contact(contact.id).ok());
  EXPECT_EQ(touchscreen.touchscreen->submit_count(), 2U);
  EXPECT_TRUE(touchscreen.touchscreen->close().ok());
  EXPECT_EQ(touchscreen.touchscreen->place_contact(contact).code(), lvh::ErrorCode::device_closed);

  auto trackpad = runtime->create_trackpad();
  ASSERT_TRUE(trackpad);
  ASSERT_NE(trackpad.trackpad, nullptr);
  EXPECT_EQ(trackpad.trackpad->profile().device_type, lvh::DeviceType::trackpad);
  EXPECT_TRUE(trackpad.trackpad->place_contact(contact).ok());
  EXPECT_TRUE(trackpad.trackpad->button(true).ok());
  EXPECT_TRUE(trackpad.trackpad->release_contact(contact.id).ok());
  EXPECT_EQ(trackpad.trackpad->submit_count(), 3U);
  EXPECT_TRUE(trackpad.trackpad->close().ok());
  EXPECT_EQ(trackpad.trackpad->button(false).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesPenTablet) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_pen_tablet();

  ASSERT_TRUE(created);
  ASSERT_NE(created.pen_tablet, nullptr);
  EXPECT_EQ(created.pen_tablet->profile().device_type, lvh::DeviceType::pen_tablet);

  lvh::PenToolState tool {
    .tool = lvh::PenToolType::pen,
    .x = 0.25F,
    .y = 0.5F,
    .pressure = 0.75F,
    .distance = -1.0F,
    .tilt_x = 45.0F,
    .tilt_y = -45.0F,
  };

  EXPECT_TRUE(created.pen_tablet->place_tool(tool).ok());
  EXPECT_TRUE(created.pen_tablet->button(lvh::PenButton::primary, true).ok());
  EXPECT_EQ(created.pen_tablet->submit_count(), 2U);
  EXPECT_EQ(created.pen_tablet->last_submitted_tool().tool, lvh::PenToolType::pen);
  EXPECT_TRUE(created.pen_tablet->close().ok());
  EXPECT_EQ(created.pen_tablet->button(lvh::PenButton::primary, false).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, RejectsNonFinitePointerInputBeforeSubmission) {
  auto runtime = lvh::Runtime::create();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const auto infinity = std::numeric_limits<float>::infinity();

  auto mouse = runtime->create_mouse();
  ASSERT_TRUE(mouse);
  EXPECT_EQ(mouse.mouse->move_absolute(nan, 0.5F, 1, 1).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(mouse.mouse->move_absolute(0.5F, infinity, 1, 1).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(mouse.mouse->submit_count(), 0U);
  EXPECT_TRUE(mouse.mouse->move_absolute(0.5F, 0.5F, 1, 1).ok());

  auto touch = runtime->create_touchscreen();
  auto trackpad = runtime->create_trackpad();
  ASSERT_TRUE(touch);
  ASSERT_TRUE(trackpad);
  lvh::TouchContact contact {.id = 1, .x = 0.25F, .y = 0.5F, .pressure = 0.5F};
  const auto check_touch_invalid = [&](const lvh::TouchContact &invalid) {
    EXPECT_EQ(touch.touchscreen->place_contact(invalid).code(), lvh::ErrorCode::invalid_argument);
    EXPECT_EQ(trackpad.trackpad->place_contact(invalid).code(), lvh::ErrorCode::invalid_argument);
  };
  contact.x = nan;
  check_touch_invalid(contact);
  contact.x = 0.25F;
  contact.y = infinity;
  check_touch_invalid(contact);
  contact.y = 0.5F;
  contact.pressure = nan;
  check_touch_invalid(contact);
  contact.pressure = 0.5F;
  contact.contact_major_axis = infinity;
  check_touch_invalid(contact);
  contact.contact_major_axis = 0.0F;
  contact.contact_minor_axis = nan;
  check_touch_invalid(contact);
  EXPECT_EQ(touch.touchscreen->submit_count(), 0U);
  EXPECT_EQ(trackpad.trackpad->submit_count(), 0U);

  contact.contact_minor_axis = 0.0F;
  EXPECT_TRUE(touch.touchscreen->place_contact(contact).ok());
  EXPECT_TRUE(trackpad.trackpad->place_contact(contact).ok());

  auto pen = runtime->create_pen_tablet();
  ASSERT_TRUE(pen);
  lvh::PenToolState state {.x = 0.5F, .y = 0.5F, .pressure = 0.5F};
  const auto check_pen_invalid = [&](const lvh::PenToolState &invalid) {
    EXPECT_EQ(pen.pen_tablet->place_tool(invalid).code(), lvh::ErrorCode::invalid_argument);
  };
  state.x = nan;
  check_pen_invalid(state);
  state.x = 0.5F;
  state.y = infinity;
  check_pen_invalid(state);
  state.y = 0.5F;
  state.pressure = nan;
  check_pen_invalid(state);
  state.pressure = 0.5F;
  state.distance = infinity;
  check_pen_invalid(state);
  state.distance = -1.0F;
  state.tilt_x = nan;
  check_pen_invalid(state);
  state.tilt_x = 0.0F;
  state.tilt_y = infinity;
  check_pen_invalid(state);
  EXPECT_EQ(pen.pen_tablet->submit_count(), 0U);

  // Release/cancel/leave packets may contain undefined analog fields.
  state.transition = lvh::PointerTransition::release;
  EXPECT_TRUE(pen.pen_tablet->place_tool(state).ok());
  state.transition = lvh::PointerTransition::cancel;
  EXPECT_TRUE(pen.pen_tablet->place_tool(state).ok());
  state.transition = lvh::PointerTransition::leave;
  EXPECT_TRUE(pen.pen_tablet->place_tool(state).ok());
  EXPECT_EQ(pen.pen_tablet->submit_count(), 3U);
}

TEST(RuntimeTest, ValidatesOptionalTouchAndPenDesktopGeometry) {
  auto runtime = lvh::Runtime::create();

  lvh::CreateTouchscreenOptions touch_options;
  touch_options.profile = lvh::profiles::touchscreen();
  touch_options.desktop = {.width = 1920};
  EXPECT_EQ(runtime->create_touchscreen(touch_options).status.code(), lvh::ErrorCode::invalid_argument);

  touch_options.desktop = {.offset_x = -1920, .width = 3840, .height = 1080};
  auto touch = runtime->create_touchscreen(touch_options);
  ASSERT_TRUE(touch);

  const lvh::TouchContact valid {
    .id = 1,
    .x = 0.5F,
    .y = 0.5F,
    .pressure = 0.5F,
    .viewport = {.offset_x = 0, .width = 1920, .height = 1080},
  };
  EXPECT_TRUE(touch.touchscreen->place_contact(valid).ok());
  auto invalid = valid;
  invalid.viewport.offset_x = 1920;
  EXPECT_EQ(touch.touchscreen->place_contact(invalid).code(), lvh::ErrorCode::invalid_argument);
  invalid = valid;
  invalid.viewport.height = 0;
  EXPECT_EQ(touch.touchscreen->place_contact(invalid).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(touch.touchscreen->submit_count(), 1U);

  lvh::CreatePenTabletOptions pen_options;
  pen_options.profile = lvh::profiles::pen_tablet();
  pen_options.desktop = {.width = -1, .height = 1080};
  EXPECT_EQ(runtime->create_pen_tablet(pen_options).status.code(), lvh::ErrorCode::invalid_argument);

  pen_options.desktop = touch_options.desktop;
  auto pen = runtime->create_pen_tablet(pen_options);
  ASSERT_TRUE(pen);
  lvh::PenToolState pen_state {
    .tool = lvh::PenToolType::pen,
    .pressure = 0.5F,
    .viewport = {.offset_x = 0, .width = 1920, .height = 1080},
  };
  EXPECT_TRUE(pen.pen_tablet->place_tool(pen_state).ok());
  pen_state.viewport.offset_x = 1920;
  EXPECT_EQ(pen.pen_tablet->place_tool(pen_state).code(), lvh::ErrorCode::invalid_argument);
  pen_state.transition = lvh::PointerTransition::release;
  EXPECT_TRUE(pen.pen_tablet->place_tool(pen_state).ok());
  EXPECT_EQ(pen.pen_tablet->submit_count(), 2U);
}

TEST_F(LinuxRuntimeTest, LinuxUhidSmokeTestRequiresPrerequisites) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad);

  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
  ASSERT_TRUE(created) << created.status.message();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.left_stick.x = 0.25F;
  state.right_trigger = 1.0F;

  EXPECT_TRUE(created.gamepad->submit(state).ok());
  EXPECT_TRUE(created.gamepad->close().ok());
}

TEST_F(LinuxRuntimeTest, LinuxUinputSmokeTestRequiresPrerequisites) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);
  const auto &capabilities = runtime->capabilities();

  ASSERT_TRUE(capabilities.supports_keyboard);
  auto keyboard = runtime->create_keyboard();
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  EXPECT_TRUE(keyboard.keyboard->press(0x41).ok());
  EXPECT_TRUE(keyboard.keyboard->release(0x41).ok());

  ASSERT_TRUE(capabilities.supports_mouse);
  auto mouse = runtime->create_mouse();
  ASSERT_TRUE(mouse) << mouse.status.message();
  EXPECT_TRUE(mouse.mouse->move_relative(1, 1).ok());
  EXPECT_TRUE(mouse.mouse->vertical_scroll(120).ok());

  ASSERT_TRUE(capabilities.supports_touchscreen);
  auto touchscreen = runtime->create_touchscreen();
  ASSERT_TRUE(touchscreen) << touchscreen.status.message();
  EXPECT_TRUE(touchscreen.touchscreen->place_contact({.id = 1, .x = 0.5F, .y = 0.25F, .pressure = 1.0F}).ok());
  EXPECT_TRUE(touchscreen.touchscreen->release_contact(1).ok());

  ASSERT_TRUE(capabilities.supports_trackpad);
  auto trackpad = runtime->create_trackpad();
  ASSERT_TRUE(trackpad) << trackpad.status.message();
  EXPECT_TRUE(trackpad.trackpad->place_contact({.id = 1, .x = 0.5F, .y = 0.25F, .pressure = 1.0F}).ok());
  EXPECT_TRUE(trackpad.trackpad->button(true).ok());
  EXPECT_TRUE(trackpad.trackpad->button(false).ok());
  EXPECT_TRUE(trackpad.trackpad->release_contact(1).ok());

  ASSERT_TRUE(capabilities.supports_pen_tablet);
  auto pen_tablet = runtime->create_pen_tablet();
  ASSERT_TRUE(pen_tablet) << pen_tablet.status.message();
  EXPECT_TRUE(
    pen_tablet.pen_tablet
      ->place_tool({.tool = lvh::PenToolType::pen, .x = 0.5F, .y = 0.25F, .pressure = 0.75F, .tilt_x = 10.0F})
      .ok()
  );
  EXPECT_TRUE(pen_tablet.pen_tablet->button(lvh::PenButton::primary, true).ok());
  EXPECT_TRUE(pen_tablet.pen_tablet->button(lvh::PenButton::primary, false).ok());
}
