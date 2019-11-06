project('Scidown', 'c',
  version : '2018.01.09',
  license : 'GPL3'
)

PROJECT_NAME = 'scidown'

charter_sources = [
	'src/charter/src/clist.c',
    'src/charter/src/parser.c',
    'src/charter/src/charter.c',
    'src/charter/src/svg.c',
    'src/charter/src/latex.c',
    'src/charter/src/charter_string.c',
    'src/charter/src/svg_utils.c',
    'src/charter/src/tinyexpr/tinyexpr.c',
    'src/charter/src/csv_parser/csvparser.c'
]

lib_sources = [
    'src/utils.c',
    'src/constants.c',
    'src/autolink.c',
    'src/buffer.c',
    'src/document.c',
    'src/escape.c',
    'src/html_blocks.c',
    'src/html.c',
    'src/latex.c',
    'src/html_smartypants.c',
    'src/stack.c',
    'src/version.c'
]


bin_sources = [
  'bin/scidown.c'
]

deps = []

shared_library(
    PROJECT_NAME,
    sources: [charter_sources, lib_sources],
    link_args: '-lm',
    dependencies : deps,
    install: true
)

executable(
    PROJECT_NAME,
    sources: [charter_sources, lib_sources, bin_sources],
    link_args: '-lm',
    c_args: ['-I../src/'],
    dependencies : deps,
    install: true
)
