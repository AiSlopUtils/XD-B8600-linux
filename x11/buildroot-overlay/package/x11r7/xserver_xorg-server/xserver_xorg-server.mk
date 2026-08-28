################################################################################
#
# xserver_xorg-server
#
################################################################################

XSERVER_XORG_SERVER_VERSION = $(call qstrip,$(BR2_PACKAGE_XSERVER_XORG_SERVER_VERSION))
XSERVER_XORG_SERVER_SOURCE = xorg-server-$(XSERVER_XORG_SERVER_VERSION).tar.bz2
XSERVER_XORG_SERVER_SITE = https://xorg.freedesktop.org/archive/individual/xserver
XSERVER_XORG_SERVER_LICENSE = MIT
XSERVER_XORG_SERVER_LICENSE_FILES = COPYING
XSERVER_XORG_SERVER_INSTALL_STAGING = YES
# xfont_font-util is needed only for autoreconf
XSERVER_XORG_SERVER_AUTORECONF = YES
XSERVER_XORG_SERVER_DEPENDENCIES = \
	xfont_font-util \
	xutil_util-macros \
	xlib_libXau \
	xlib_libXfont2 \
	xlib_libxkbfile \
	xlib_xtrans \
	xorgproto \
	xkeyboard-config \
	pixman \
	host-pkgconf

# We force -O2 regardless of the optimization level chosen by the
# user, as the X.org server is known to trigger some compiler bugs at
# -Os on several architectures.
XSERVER_XORG_SERVER_CONF_OPTS = \
	--disable-config-hal \
	--disable-config-udev \
	--disable-config-wscons \
	--disable-listen-local \
	--disable-local-transport \
	--disable-docs \
	--disable-devel-docs \
	--disable-record \
	--disable-composite \
	--disable-screensaver \
	--disable-tslib \
	--disable-mitshm \
	--disable-xres \
	--disable-xv \
	--disable-xvmc \
	--disable-dga \
	--disable-present \
	--disable-xinerama \
	--disable-dbe \
	--disable-dpms \
	--disable-xdmcp \
	--disable-xdm-auth-1 \
	--disable-xf86vidmode \
	--disable-xace \
	--disable-xselinux \
	--disable-xcsecurity \
	--disable-xf86bigfont \
	--disable-xfree86-utils \
	--disable-vgahw \
	--disable-vbe \
	--disable-int10-module \
	--disable-clientids \
	--disable-xshmfence \
	--disable-suid-wrapper \
	--disable-install-setuid \
	--enable-listen-unix \
	--disable-listen-tcp \
	--enable-unix-transport \
	--disable-tcp-transport \
	--disable-ipv6 \
	--disable-secure-rpc \
	--disable-input-thread \
	--disable-xtrans-send-fds \
	ac_cv_func_epoll_create1=no \
	--disable-xnest \
	--disable-xephyr \
	--disable-xfake \
	--disable-dmx \
	--disable-unit-tests \
	--with-builder-addr=buildroot@buildroot.org \
	CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/pixman-1 -O2 -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables" \
	LDFLAGS="$(TARGET_LDFLAGS) -Wl,--gc-sections" \
	--with-fontrootdir=/usr/share/fonts/X11/ \
	--with-default-font-path=built-ins \
	--with-default-xkb-rules=evdev \
	--with-default-xkb-model=pc105 \
	--with-default-xkb-layout=us \
	--with-xkb-bin-directory=/opt/x11/usr/bin \
	--with-xkb-path=/opt/x11/usr/share/X11/xkb \
	--with-xkb-output=/tmp/xkb \
	--$(if $(BR2_PACKAGE_XSERVER_XORG_SERVER_XVFB),en,dis)able-xvfb

ifeq ($(BR2_PACKAGE_SYSTEMD),y)
XSERVER_XORG_SERVER_CONF_OPTS += \
	--with-systemd-daemon \
	--enable-systemd-logind
XSERVER_XORG_SERVER_DEPENDENCIES += \
	systemd
else
XSERVER_XORG_SERVER_CONF_OPTS += \
	--without-systemd-daemon \
	--disable-systemd-logind
endif

# Xwayland support needs libdrm, libepoxy, wayland and libxcomposite
ifeq ($(BR2_PACKAGE_LIBDRM)$(BR2_PACKAGE_LIBEPOXY)$(BR2_PACKAGE_WAYLAND)$(BR2_PACKAGE_WAYLAND_PROTOCOLS)$(BR2_PACKAGE_XLIB_LIBXCOMPOSITE),yyyyy)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-xwayland
XSERVER_XORG_SERVER_DEPENDENCIES += libdrm libepoxy wayland wayland-protocols xlib_libXcomposite
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-xwayland
endif

ifeq ($(BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-xorg
XSERVER_XORG_SERVER_DEPENDENCIES += libpciaccess
ifeq ($(BR2_PACKAGE_LIBDRM),y)
XSERVER_XORG_SERVER_DEPENDENCIES += libdrm
XSERVER_XORG_SERVER_CONF_OPTS += --enable-libdrm
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-libdrm
endif
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-xorg
endif

ifeq ($(BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE),y)
XSERVER_XORG_SERVER_CONF_OPTS += \
	--enable-kdrive \
	--enable-xfbdev \
	--disable-glx \
	--disable-dri \
	--disable-xsdl
define XSERVER_CREATE_X_SYMLINK
	ln -f -s Xfbdev $(TARGET_DIR)/usr/bin/X
endef
XSERVER_XORG_SERVER_POST_INSTALL_TARGET_HOOKS += XSERVER_CREATE_X_SYMLINK

ifeq ($(BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE_EVDEV),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-kdrive-evdev
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-kdrive-evdev
endif

ifeq ($(BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE_KBD),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-kdrive-kbd
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-kdrive-kbd
endif

ifeq ($(BR2_PACKAGE_XSERVER_XORG_SERVER_KDRIVE_MOUSE),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-kdrive-mouse
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-kdrive-mouse
endif

else # modular
XSERVER_XORG_SERVER_CONF_OPTS += --disable-kdrive --disable-xfbdev
endif

ifeq ($(BR2_PACKAGE_MESA3D_DRI_DRIVER),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-dri --enable-glx
XSERVER_XORG_SERVER_DEPENDENCIES += mesa3d
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-dri --disable-glx
endif

ifeq ($(BR2_PACKAGE_XSERVER_XORG_SERVER_AIGLX),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-aiglx
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-aiglx
endif

# Optional packages
ifeq ($(BR2_PACKAGE_TSLIB),y)
XSERVER_XORG_SERVER_DEPENDENCIES += tslib
XSERVER_XORG_SERVER_CONF_OPTS += --enable-tslib LDFLAGS="-lts"
endif

ifeq ($(BR2_PACKAGE_HAS_UDEV),y)
XSERVER_XORG_SERVER_DEPENDENCIES += udev
XSERVER_XORG_SERVER_CONF_OPTS += --enable-config-udev
# udev kms support depends on libdrm and dri2
ifeq ($(BR2_PACKAGE_LIBDRM),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-config-udev-kms
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-config-udev-kms
endif
endif

ifeq ($(BR2_PACKAGE_DBUS),y)
XSERVER_XORG_SERVER_DEPENDENCIES += dbus
XSERVER_XORG_SERVER_CONF_OPTS += --enable-config-dbus
endif

ifeq ($(BR2_PACKAGE_FREETYPE),y)
XSERVER_XORG_SERVER_DEPENDENCIES += freetype
endif

ifeq ($(BR2_PACKAGE_LIBUNWIND),y)
XSERVER_XORG_SERVER_DEPENDENCIES += libunwind
XSERVER_XORG_SERVER_CONF_OPTS += --enable-libunwind
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-libunwind
endif

ifeq ($(BR2_PACKAGE_XLIB_LIBXFONT2),y)
XSERVER_XORG_SERVER_DEPENDENCIES += xlib_libXfont2
endif

ifeq ($(BR2_PACKAGE_XLIB_LIBXFONT),y)
XSERVER_XORG_SERVER_DEPENDENCIES += xlib_libXfont
endif

ifneq ($(BR2_PACKAGE_XLIB_LIBXVMC),y)
XSERVER_XORG_SERVER_CONF_OPTS += --disable-xvmc
endif

ifeq ($(BR2_PACKAGE_XLIB_LIBXCOMPOSITE),y)
XSERVER_XORG_SERVER_DEPENDENCIES += xlib_libXcomposite
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-composite
endif

ifeq ($(BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR),y)
XSERVER_XORG_SERVER_CONF_OPTS += --enable-dri2
ifeq ($(BR2_PACKAGE_XLIB_LIBXSHMFENCE),y)
XSERVER_XORG_SERVER_DEPENDENCIES += xlib_libxshmfence
XSERVER_XORG_SERVER_CONF_OPTS += --enable-dri3
ifeq ($(BR2_PACKAGE_HAS_LIBEGL)$(BR2_PACKAGE_HAS_LIBGL)$(BR2_PACKAGE_LIBEPOXY),yyy)
XSERVER_XORG_SERVER_DEPENDENCIES += libepoxy
XSERVER_XORG_SERVER_CONF_OPTS += --enable-glamor
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-glamor
endif
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-dri3 --disable-glamor
endif
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-dri2 --disable-dri3 --disable-glamor
endif

ifeq ($(BR2_PACKAGE_XLIB_LIBXSCRNSAVER),y)
XSERVER_XORG_SERVER_DEPENDENCIES += xlib_libXScrnSaver
XSERVER_XORG_SERVER_CONF_OPTS += --enable-screensaver
else
XSERVER_XORG_SERVER_CONF_OPTS += --disable-screensaver
endif

ifneq ($(BR2_PACKAGE_XLIB_LIBDMX),y)
XSERVER_XORG_SERVER_CONF_OPTS += --disable-dmx
endif

ifeq ($(BR2_PACKAGE_OPENSSL),y)
XSERVER_XORG_SERVER_CONF_OPTS += --with-sha1=libcrypto
XSERVER_XORG_SERVER_DEPENDENCIES += openssl
else ifeq ($(BR2_PACKAGE_LIBGCRYPT),y)
XSERVER_XORG_SERVER_CONF_OPTS += --with-sha1=libgcrypt
XSERVER_XORG_SERVER_DEPENDENCIES += libgcrypt
else
XSERVER_XORG_SERVER_CONF_OPTS += --with-sha1=libsha1
XSERVER_XORG_SERVER_DEPENDENCIES += libsha1
endif

define XSERVER_XORG_SERVER_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 755 package/x11r7/xserver_xorg-server/S40xorg \
		$(TARGET_DIR)/etc/init.d/S40xorg
endef

$(eval $(autotools-package))
