# Hikari - Wayland Compositor

![Screenshot](https://acmelabs.space/~raichoo/hikari.png)

## Description

_hikari_ is a stacking Wayland compositor with additional tiling capabilities,
it is heavily inspired by the Calm Window manager (cwm(1)). Its core concepts
are *views*, *groups*, *sheets* and the *workspace*.

The workspace is the set of views that are currently visible.

A sheet is a collection of views, each view can only be a member of a single
sheet. Switching between sheets will replace the current content of the
workspace with all the views that are a member of the selected sheet. _hikari_
has 9 general purpose sheets that correspond to the numbers **1** to **9** and a
special purpose sheet **0**. Views that are a member of sheet **0** will
always be visible but stacked below the views of the selected sheet.

Groups are a bit more fine grained than sheets. Like sheets, groups are a
collection of views. Unlike sheets you can have a arbitrary number of groups
and each group can have an arbitrary name. Views from one group can be spread
among all available sheets. Some operations act on entire groups rather than
individual views.

## Setting up Wayland on FreeBSD

Wayland currently requires some care to work properly on FreeBSD. This section
aims to document the recent state of how to enable Wayland on the FreeBSD
`STABLE` branch and will change once support is being improved.

### Mouse configuration

To make mice work `kern.evdev.rcpt_mask` should be set to `12`. Depending on
your version of FreeBSD this is done automatically or via setting the value in
`/etc/sysctl.conf`.

Some systems might require `moused` for mice to work. Enable it with `service
moused enable`. This requires setting `kern.evdev.rcpt_mask` to `3`.

### Setting up XDG\_RUNTIME\_DIR

This section describes how to use `/tmp` as your `XDG_RUNTIME_DIR`. Some Wayland
clients (e.g. native Wayland `firefox`) require `posix_fallocate` to work in
that directory. This is not supported by ZFS, therefore you should prevent the
ZFS tmp dataset from mounting to `/tmp` and `mount -t tmpfs tmpfs /tmp`. To
persist this setting edit your `/etc/fstab` appropriately to automatically mount
`tmpfs` during boot.

Additionally set `XDG_RUNTIME_DIR` to `/tmp` in your environment.

### Setting up PAM

Setting up PAM is needed to give `hikari` the ability to unlock the screen when
using the screen locker. Copy the appropriate `hikari-unlocker` file from the
`pam.d` folder to `/usr/local/etc/pam.d` (or `/etc/pam.d` on most Linux
systems).

### Setting up the keyboard layout

`hikari` gets its keyboard settings from `xkb` environment variables. To select
a layout set the `XKB_DEFAULT_LAYOUT` to the desired value before staring
`hikari`.

```
XKB_DEFAULT_LAYOUT "de(nodeadkeys),de"
```

## Building

`hikari` currently only works on FreeBSD and Linux. This will likely change in
the future when more systems adopt Wayland.

### Dependencies

* wlroots
* pango
* cairo
* libinput
* xkbcommon
* pixman
* libucl
* glib
* evdev-proto
* epoll-shim (FreeBSD)

### Compiling and Installing

The build process will produce two binaries `hikari` and `hikari-unlocker`. The
latter one is used to check credentials for unlocking the screen. Both need to
be installed with root setuid in your `PATH`.

`hikari` can be configured via `$HOME/.config/hikari/hikari.conf`, an example
can be found under `share/exampes/hikari/hikari.conf`.

#### Building on FreeBSD

Simply run `make`. Installation needs `PREFIX` and `ETC_PREFIX` to be defined.
To install everything in `/usr/local` run

```
make PREFIX=/usr/local ETC_PREFIX=/usr/local/etc install
```

`uninstall` requires the same values for `PREFIX` and `ETC_PREFIX`.

#### Building on Linux

On Linux `bmake` is required which needs to be run like so:

```
bmake WITH_POSIX_C_SOURCE=YES
```

Installation needs `PREFIX` and `ETC_PREFIX` to be defined. To install
everything in `/usr/local` run.

```
bmake PREFIX=/usr/local ETC_PREFIX=/etc install
```

`pam.d` files should be installed in `/etc` or `/usr/lib/pam.d` depending on
your distribution.

`uninstall` requires the same values for `PREFIX` and `ETC_PREFIX`.

#### Building with XWayland support

`hikari` offers optional XWayland support which is enabled via setting
`WITH_XWAYLAND`.

```
make WITH_XWAYLAND=YES
```

#### Building with screencopy support

Screencopy support allows tools like `grim` to work with `hikari`, it also
allows applications to copy the desktop content. This is disabled by default
and can be added by setting `WITH_SCREENCOPY`.

```
make WITH_SCREENCOPY=YES
```

#### Building with gammacontrol support

Gamma control is needed for tools like `redshift`. This is disabled by default
and can be enabled via setting `WITH_GAMMACONTROL`.

```
make WITH_GAMMACONTROL=YES
```

#### Building with layer-shell support

Some applications that are used to build desktop components require
`layer-shell`. Examples for this are `waybar`, `wofi` and `slurp`. To turn on
`layer-shell` support compile with the `WITH_LAYERSHELL` option.

```
make WITH_LAYERSHELL=YES
```

#### Building the manpage

Building the `hikari` manpage requires [`pandoc`](http://pandoc.org/). To build
the manpage just run `make VERSION=1.0.0 doc`, where `VERSION` is the version
number that will be spliced into the manpage.

## Community

The `hikari` community gears to be inclusive and welcoming to everyone, this is
why we chose to adhere to the [Geekfeminism Code of
Conduct](https://geekfeminismdotorg.wordpress.com/about/code-of-conduct/).

If you care to be a part of our community, please join our Matrix chat at
`#hikari:acmelabs.space` and/or subscribe to our mailing list by sending a mail
to `hikari+subscribe@acmelabs.space`.

## Contributing

Please make sure you use `clang-format` with the accompanying `.clang-format`
configuration before submitting any patches.
