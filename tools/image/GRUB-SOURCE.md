# GRUB on Kamos-NT boot images

`make iso` runs `grub-mkrescue`, which places GRUB 2 boot images on `build/kamos.iso`.
GRUB is free software under the GNU General Public License version 3 or later. It is an
independent program on the same medium as the Kamos-NT kernel; it is not linked with it.

- GRUB license: https://www.gnu.org/licenses/gpl-3.0.html
- GRUB source: https://ftp.gnu.org/gnu/grub/ and the Ubuntu `grub2` source package matching
  the version recorded in `toolchain/versions.txt` (`apt-get source grub2`).

Anyone receiving a Kamos-NT ISO from the project can obtain the corresponding GRUB source
from those locations, or by asking the maintainers, for at least three years after the
release.
