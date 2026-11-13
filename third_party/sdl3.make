# ------------------------------------------------------ #
# Build rules for the vendored SDL3 (third_party/SDL3/)  #
# ------------------------------------------------------ #
#
# Included by the top-level Makefile. Builds a static
# libSDL3.a from the unmodified SDL 3.4.20 sources in
# third_party/SDL3/ (provenance and hashes:
# docs/DEPENDENCIES.md, third_party/SDL3/MANIFEST.sha256).
#
# Only macOS builds the vendored copy for now. Linux and
# Windows still use the system SDL3 found by pkgconf.
#
# SDL's own build system is CMake, which this fork does
# not use. The source list and settings below are taken
# from the tarball's Xcode project
# (Xcode/SDL/SDL.xcodeproj, target "SDL3", Sources phase,
# minus the two .metal files: SDL ships their compiled
# form as headers, SDL_shaders_metal_macos.h and
# Metal_Blit.h). Files of that list that are for iOS,
# tvOS or Windows compile to nothing on macOS, as they
# do in Xcode. The configuration is the stock
# include/build_config/SDL_build_config_macos.h, which
# SDL_build_config.h selects when no CMake-generated
# config is on the include path. Nothing in the vendored
# tree is edited; settings go on the command line only.
#
# Header search paths, ARC for Objective-C, hidden
# visibility (GCC_SYMBOLS_PRIVATE_EXTERN) and the
# frameworks are the Xcode project's.
#
# ------------------------------------------------------ #

SDL3_DIR := third_party/SDL3
SDL3_LIB := $(BUILDDIR)/sdl3/libSDL3.a

# Used for the engine's own SDL-using code (client and
# renderers): the public headers only.
SDL3_INCLUDE := -I$(SDL3_DIR)/include

# Used for SDL itself.
SDL3_CPPFLAGS := -I$(SDL3_DIR)/include -I$(SDL3_DIR)/include/build_config \
	-I$(SDL3_DIR)/src -I$(SDL3_DIR)/src/hidapi/hidapi \
	-I$(SDL3_DIR)/src/video/khronos

ifdef DEBUG
SDL3_CFLAGS := -O0 -g
else
SDL3_CFLAGS := -O2 -DNDEBUG
endif
SDL3_CFLAGS += -arch $(YQ2_ARCH) -fvisibility=hidden -fno-common -pipe -w
ifdef ASAN
SDL3_CFLAGS += -fsanitize=address
endif
ifdef UBSAN
SDL3_CFLAGS += -fsanitize=undefined
endif

# Frameworks and libraries, from the Xcode project's
# Frameworks phase and OTHER_LDFLAGS (-liconv).
SDL3_LDLIBS := -liconv \
	-framework AudioToolbox -framework Carbon -framework Cocoa \
	-framework CoreAudio -framework CoreFoundation -framework CoreHaptics \
	-framework CoreVideo -framework ForceFeedback -framework GameController \
	-framework IOKit -framework Metal -framework QuartzCore \
	-framework UniformTypeIdentifiers

# Some sources reference frameworks the Xcode project
# links implicitly through module imports
# (CLANG_ENABLE_MODULES): camera (AVFoundation,
# CoreMedia) and the Metal view.
SDL3_LDLIBS += -framework AVFoundation -framework CoreMedia

SDL3_SRCS := \
	SDL.c \
	SDL_assert.c \
	SDL_error.c \
	SDL_guid.c \
	SDL_hashtable.c \
	SDL_hints.c \
	SDL_list.c \
	SDL_log.c \
	SDL_properties.c \
	SDL_utils.c \
	atomic/SDL_atomic.c \
	atomic/SDL_spinlock.c \
	audio/SDL_audio.c \
	audio/SDL_audiocvt.c \
	audio/SDL_audiodev.c \
	audio/SDL_audioqueue.c \
	audio/SDL_audioresample.c \
	audio/SDL_audiotypecvt.c \
	audio/SDL_mixer.c \
	audio/SDL_wave.c \
	audio/coreaudio/SDL_coreaudio.m \
	audio/disk/SDL_diskaudio.c \
	audio/dummy/SDL_dummyaudio.c \
	camera/SDL_camera.c \
	camera/coremedia/SDL_camera_coremedia.m \
	camera/dummy/SDL_camera_dummy.c \
	core/SDL_core_unsupported.c \
	cpuinfo/SDL_cpuinfo.c \
	dialog/SDL_dialog.c \
	dialog/SDL_dialog_utils.c \
	dialog/cocoa/SDL_cocoadialog.m \
	dialog/dummy/SDL_dummydialog.c \
	dynapi/SDL_dynapi.c \
	events/SDL_categories.c \
	events/SDL_clipboardevents.c \
	events/SDL_displayevents.c \
	events/SDL_dropevents.c \
	events/SDL_events.c \
	events/SDL_eventwatch.c \
	events/SDL_keyboard.c \
	events/SDL_keymap.c \
	events/SDL_mouse.c \
	events/SDL_pen.c \
	events/SDL_quit.c \
	events/SDL_touch.c \
	events/SDL_windowevents.c \
	filesystem/SDL_filesystem.c \
	filesystem/cocoa/SDL_sysfilesystem.m \
	filesystem/dummy/SDL_sysfilesystem.c \
	filesystem/posix/SDL_sysfsops.c \
	gpu/SDL_gpu.c \
	gpu/metal/SDL_gpu_metal.m \
	gpu/vulkan/SDL_gpu_vulkan.c \
	haptic/SDL_haptic.c \
	haptic/darwin/SDL_syshaptic.c \
	haptic/dummy/SDL_syshaptic.c \
	haptic/hidapi/SDL_hidapihaptic.c \
	haptic/hidapi/SDL_hidapihaptic_lg4ff.c \
	hidapi/SDL_hidapi.c \
	hidapi/ios/hid.m \
	io/SDL_asyncio.c \
	io/SDL_iostream.c \
	io/generic/SDL_asyncio_generic.c \
	io/windows/SDL_asyncio_windows_ioring.c \
	joystick/SDL_gamepad.c \
	joystick/SDL_joystick.c \
	joystick/SDL_steam_virtual_gamepad.c \
	joystick/apple/SDL_mfijoystick.m \
	joystick/controller_type.c \
	joystick/darwin/SDL_iokitjoystick.c \
	joystick/dummy/SDL_sysjoystick.c \
	joystick/hidapi/SDL_hidapi_8bitdo.c \
	joystick/hidapi/SDL_hidapi_combined.c \
	joystick/hidapi/SDL_hidapi_flydigi.c \
	joystick/hidapi/SDL_hidapi_gamecube.c \
	joystick/hidapi/SDL_hidapi_gip.c \
	joystick/hidapi/SDL_hidapi_lg4ff.c \
	joystick/hidapi/SDL_hidapi_luna.c \
	joystick/hidapi/SDL_hidapi_ps3.c \
	joystick/hidapi/SDL_hidapi_ps4.c \
	joystick/hidapi/SDL_hidapi_ps5.c \
	joystick/hidapi/SDL_hidapi_rumble.c \
	joystick/hidapi/SDL_hidapi_shield.c \
	joystick/hidapi/SDL_hidapi_sinput.c \
	joystick/hidapi/SDL_hidapi_stadia.c \
	joystick/hidapi/SDL_hidapi_steam.c \
	joystick/hidapi/SDL_hidapi_steam_hori.c \
	joystick/hidapi/SDL_hidapi_steam_triton.c \
	joystick/hidapi/SDL_hidapi_steamdeck.c \
	joystick/hidapi/SDL_hidapi_switch.c \
	joystick/hidapi/SDL_hidapi_switch2.c \
	joystick/hidapi/SDL_hidapi_wii.c \
	joystick/hidapi/SDL_hidapi_xbox360.c \
	joystick/hidapi/SDL_hidapi_xbox360w.c \
	joystick/hidapi/SDL_hidapi_xboxone.c \
	joystick/hidapi/SDL_hidapi_zuiki.c \
	joystick/hidapi/SDL_hidapijoystick.c \
	joystick/hidapi/SDL_report_descriptor.c \
	joystick/virtual/SDL_virtualjoystick.c \
	loadso/dlopen/SDL_sysloadso.c \
	loadso/dummy/SDL_sysloadso.c \
	locale/SDL_locale.c \
	locale/macos/SDL_syslocale.m \
	main/SDL_main_callbacks.c \
	main/SDL_runapp.c \
	main/generic/SDL_sysmain_callbacks.c \
	main/ios/SDL_sysmain_callbacks.m \
	misc/SDL_libusb.c \
	misc/SDL_url.c \
	misc/ios/SDL_sysurl.m \
	misc/macos/SDL_sysurl.m \
	power/SDL_power.c \
	power/macos/SDL_syspower.c \
	power/uikit/SDL_syspower.m \
	process/SDL_process.c \
	process/dummy/SDL_dummyprocess.c \
	process/posix/SDL_posixprocess.c \
	render/SDL_render.c \
	render/SDL_render_unsupported.c \
	render/SDL_yuv_sw.c \
	render/gpu/SDL_pipeline_gpu.c \
	render/gpu/SDL_render_gpu.c \
	render/gpu/SDL_shaders_gpu.c \
	render/metal/SDL_render_metal.m \
	render/opengl/SDL_render_gl.c \
	render/opengl/SDL_shaders_gl.c \
	render/opengles2/SDL_render_gles2.c \
	render/opengles2/SDL_shaders_gles2.c \
	render/software/SDL_blendfillrect.c \
	render/software/SDL_blendline.c \
	render/software/SDL_blendpoint.c \
	render/software/SDL_drawline.c \
	render/software/SDL_drawpoint.c \
	render/software/SDL_render_sw.c \
	render/software/SDL_triangle.c \
	sensor/SDL_sensor.c \
	sensor/coremotion/SDL_coremotionsensor.m \
	sensor/dummy/SDL_dummysensor.c \
	stdlib/SDL_crc16.c \
	stdlib/SDL_crc32.c \
	stdlib/SDL_getenv.c \
	stdlib/SDL_iconv.c \
	stdlib/SDL_malloc.c \
	stdlib/SDL_memcpy.c \
	stdlib/SDL_memmove.c \
	stdlib/SDL_memset.c \
	stdlib/SDL_murmur3.c \
	stdlib/SDL_qsort.c \
	stdlib/SDL_random.c \
	stdlib/SDL_stdlib.c \
	stdlib/SDL_string.c \
	stdlib/SDL_strtokr.c \
	storage/SDL_storage.c \
	storage/generic/SDL_genericstorage.c \
	thread/SDL_thread.c \
	thread/pthread/SDL_syscond.c \
	thread/pthread/SDL_sysmutex.c \
	thread/pthread/SDL_sysrwlock.c \
	thread/pthread/SDL_syssem.c \
	thread/pthread/SDL_systhread.c \
	thread/pthread/SDL_systls.c \
	time/SDL_time.c \
	time/unix/SDL_systime.c \
	timer/SDL_timer.c \
	timer/unix/SDL_systimer.c \
	tray/SDL_tray_utils.c \
	tray/cocoa/SDL_tray.m \
	tray/dummy/SDL_tray.c \
	video/SDL_RLEaccel.c \
	video/SDL_blit.c \
	video/SDL_blit_0.c \
	video/SDL_blit_1.c \
	video/SDL_blit_A.c \
	video/SDL_blit_N.c \
	video/SDL_blit_auto.c \
	video/SDL_blit_copy.c \
	video/SDL_blit_slow.c \
	video/SDL_bmp.c \
	video/SDL_clipboard.c \
	video/SDL_egl.c \
	video/SDL_fillrect.c \
	video/SDL_pixels.c \
	video/SDL_rect.c \
	video/SDL_rotate.c \
	video/SDL_stb.c \
	video/SDL_stretch.c \
	video/SDL_surface.c \
	video/SDL_video.c \
	video/SDL_video_unsupported.c \
	video/SDL_vulkan_utils.c \
	video/SDL_yuv.c \
	video/cocoa/SDL_cocoaclipboard.m \
	video/cocoa/SDL_cocoaevents.m \
	video/cocoa/SDL_cocoakeyboard.m \
	video/cocoa/SDL_cocoamessagebox.m \
	video/cocoa/SDL_cocoametalview.m \
	video/cocoa/SDL_cocoamodes.m \
	video/cocoa/SDL_cocoamouse.m \
	video/cocoa/SDL_cocoaopengl.m \
	video/cocoa/SDL_cocoaopengles.m \
	video/cocoa/SDL_cocoapen.m \
	video/cocoa/SDL_cocoashape.m \
	video/cocoa/SDL_cocoavideo.m \
	video/cocoa/SDL_cocoavulkan.m \
	video/cocoa/SDL_cocoawindow.m \
	video/dummy/SDL_nullevents.c \
	video/dummy/SDL_nullframebuffer.c \
	video/dummy/SDL_nullvideo.c \
	video/offscreen/SDL_offscreenevents.c \
	video/offscreen/SDL_offscreenframebuffer.c \
	video/offscreen/SDL_offscreenopengles.c \
	video/offscreen/SDL_offscreenvideo.c \
	video/offscreen/SDL_offscreenvulkan.c \
	video/offscreen/SDL_offscreenwindow.c \
	video/uikit/SDL_uikitappdelegate.m \
	video/uikit/SDL_uikitclipboard.m \
	video/uikit/SDL_uikitevents.m \
	video/uikit/SDL_uikitmessagebox.m \
	video/uikit/SDL_uikitmetalview.m \
	video/uikit/SDL_uikitmodes.m \
	video/uikit/SDL_uikitopengles.m \
	video/uikit/SDL_uikitopenglview.m \
	video/uikit/SDL_uikitpen.m \
	video/uikit/SDL_uikitvideo.m \
	video/uikit/SDL_uikitview.m \
	video/uikit/SDL_uikitviewcontroller.m \
	video/uikit/SDL_uikitvulkan.m \
	video/uikit/SDL_uikitwindow.m \
	video/yuv2rgb/yuv_rgb_lsx.c \
	video/yuv2rgb/yuv_rgb_sse.c \
	video/yuv2rgb/yuv_rgb_std.c

SDL3_OBJS := $(patsubst %,$(BUILDDIR)/sdl3/%.o,$(basename $(SDL3_SRCS)))

# -MMD -MP write a .d file next to each object (header
# dependencies, picked up by the -include below). Every
# SDL object also depends on this file, so a change to
# the flags or the source list rebuilds all of them.
SDL3_DEPFLAGS := -MMD -MP

$(BUILDDIR)/sdl3/%.o: $(SDL3_DIR)/src/%.c third_party/sdl3.make
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(SDL3_DEPFLAGS) $(SDL3_CFLAGS) $(SDL3_CPPFLAGS) -o $@ $<

$(BUILDDIR)/sdl3/%.o: $(SDL3_DIR)/src/%.m third_party/sdl3.make
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(SDL3_DEPFLAGS) $(SDL3_CFLAGS) -fobjc-arc $(SDL3_CPPFLAGS) -o $@ $<

-include $(SDL3_OBJS:.o=.d)

$(SDL3_LIB): $(SDL3_OBJS)
	@echo "===> AR $@"
	${Q}rm -f $@
	${Q}ar rcs $@ $(SDL3_OBJS)
