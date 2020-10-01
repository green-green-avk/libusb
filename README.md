# Non-rooted Android [LibUsb Manager](https://github.com/green-green-avk/LibUsbManager) support variant

`--enable-android-helper=<service_socket_name>` option has been added into the `configure` script in order to provide USB access through Android API with help of **Another Term [Lite]** built-in service (so, no device rooting is required).

See also:
* <https://github.com/green-green-avk/libusb-1.0.23-android-helper-service-patch> - original repository, to be used in production at the moment.
* [The server side source code (Another Term)](https://github.com/green-green-avk/LibUsbManager).
* [The server side source code (Another Term Lite) *deprecated*](https://github.com/green-green-avk/AnotherTermLite/tree/master/libusbmanager).
* [How to use (Another Term)](https://green-green-avk.github.io/AnotherTerm-docs/installing-libusb-for-nonrooted-android.html#main_content).
* [How to use (Another Term Lite) *deprecated*](https://github.com/green-green-avk/AnotherTermLite/wiki/Installing-libusb-for-nonrooted-Android).

Derived from: <https://github.com/libusb/libusb>.

---

# libusb

[![Build Status](https://travis-ci.org/libusb/libusb.svg?branch=master)](https://travis-ci.org/libusb/libusb)
[![Build status](https://ci.appveyor.com/api/projects/status/xvrfam94jii4a6lw?svg=true)](https://ci.appveyor.com/project/LudovicRousseau/libusb)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/2180/badge.svg)](https://scan.coverity.com/projects/libusb-libusb)

libusb is a library for USB device access from Linux, macOS,
Windows, OpenBSD/NetBSD and Haiku userspace.
It is written in C (Haiku backend in C++) and licensed under the GNU
Lesser General Public License version 2.1 or, at your option, any later
version (see [COPYING](COPYING)).

libusb is abstracted internally in such a way that it can hopefully
be ported to other operating systems. Please see the [PORTING](PORTING)
file for more information.

libusb homepage:
http://libusb.info/

Developers will wish to consult the API documentation:
http://api.libusb.info

Use the mailing list for questions, comments, etc:
http://mailing-list.libusb.info

- Hans de Goede <hdegoede@redhat.com>
- Xiaofan Chen <xiaofanc@gmail.com>
- Ludovic Rousseau <ludovic.rousseau@gmail.com>
- Nathan Hjelm <hjelmn@cs.unm.edu>
- Chris Dickens <christopher.a.dickens@gmail.com>

(Please use the mailing list rather than mailing developers directly)
