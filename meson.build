#  This file is part of DirectFB.
#
#  This library is free software; you can redistribute it and/or
#  modify it under the terms of the GNU Lesser General Public
#  License as published by the Free Software Foundation; either
#  version 2.1 of the License, or (at your option) any later version.
#
#  This library is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this library; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA

project('DirectFB2-lirc', 'c',
        version: '2.0.0')

directfb_dep = dependency('directfb-internal')

moduledir = directfb_dep.get_variable(pkgconfig: 'moduledir')

lirc = []
lirc_client_dep = []
if get_option('lirc_client')
  lirc_client_dep = dependency('lirc', required: false)

  if lirc_client_dep.found()
    add_global_arguments('-DHAVE_LIRC_CLIENT', language: 'c')
    lirc = 'lirc'
  endif
endif

pkgconfig = import('pkgconfig')

library('directfb_lirc',
        'lirc.c',
        dependencies: [directfb_dep, lirc_client_dep],
        install: true,
        install_dir: join_paths(moduledir, 'inputdrivers'))

pkgconfig.generate(filebase: 'directfb-inputdriver-lirc',
                   variables: 'moduledir=' + moduledir,
                   name: 'DirectFB-inputdriver-lirc',
                   description: 'LIRC input driver',
                   requires_private: lirc,
                   libraries_private: ['-L${moduledir}/inputdrivers',
                                       '-Wl,--whole-archive -ldirectfb_lirc -Wl,--no-whole-archive'])

if get_option('default_library') != 'shared'
  meson.add_install_script('dfb-update-pkgconfig', skip_if_destdir: true)
endif
