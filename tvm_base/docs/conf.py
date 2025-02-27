# -*- coding: utf-8 -*-

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

#
# documentation build configuration file, created by
# sphinx-quickstart on Thu Jul 23 19:40:08 2015.
#
# This file is execfile()d with the current directory set to its
# containing dir.
#
# Note that not all possible configuration values are present in this
# autogenerated file.
#
# All configuration values have a default; values that are commented out
# serve to show the default.
import gc
import sys
import inspect
import os, subprocess
import shlex
import sphinx_gallery

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
curr_path = os.path.dirname(os.path.abspath(os.path.expanduser(__file__)))
sys.path.insert(0, os.path.join(curr_path, "../python/"))
sys.path.insert(0, os.path.join(curr_path, "../vta/python"))

# -- General configuration ------------------------------------------------

# General information about the project.
project = "tvm"
author = "Apache Software Foundation"
copyright = "2020 - 2021, %s" % author
github_doc_root = "https://github.com/apache/tvm/tree/main/docs/"

os.environ["TVM_BUILD_DOC"] = "1"


def git_describe_version(original_version):
    """Get git describe version."""
    ver_py = os.path.join(curr_path, "..", "version.py")
    libver = {"__file__": ver_py}
    exec(compile(open(ver_py, "rb").read(), ver_py, "exec"), libver, libver)
    _, gd_version = libver["git_describe_version"]()
    if gd_version != original_version:
        print("Use git describe based version %s" % gd_version)
    return gd_version


# Version information.
import tvm
from tvm import topi
from tvm import te
from tvm import testing

version = git_describe_version(tvm.__version__)
release = version

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom ones
extensions = [
    "sphinx.ext.autodoc",
    "sphinx.ext.autosummary",
    "sphinx.ext.intersphinx",
    "sphinx.ext.napoleon",
    "sphinx.ext.mathjax",
    "sphinx_gallery.gen_gallery",
    "autodocsumm",
]

# Add any paths that contain templates here, relative to this directory.
templates_path = ["_templates"]

# The suffix(es) of source filenames.
# You can specify multiple suffix as a list of string:
# source_suffix = ['.rst', '.md']
source_suffix = [".rst", ".md"]

# The encoding of source files.
# source_encoding = 'utf-8-sig'

# generate autosummary even if no references
autosummary_generate = True

# The master toctree document.
master_doc = "index"

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#
# This is also used if you do content translation via gettext catalogs.
# Usually you set "language" from the command line for these cases.
language = None

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
# today = ''
# Else, today_fmt is used as the format for a strftime call.
# today_fmt = '%B %d, %Y'

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
exclude_patterns = ["_build"]

# The reST default role (used for this markup: `text`) to use for all
# documents.
# default_role = None

# If true, '()' will be appended to :func: etc. cross-reference text.
# add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
# add_module_names = True

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
# show_authors = False

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = "sphinx"

# A list of ignored prefixes for module index sorting.
# modindex_common_prefix = []

# If true, keep warnings as "system message" paragraphs in the built documents.
# keep_warnings = False

# If true, `todo` and `todoList` produce output, else they produce nothing.
todo_include_todos = False

# -- Options for HTML output ----------------------------------------------

# The theme is set by the make target
html_theme = os.environ.get("TVM_THEME", "rtd")

on_rtd = os.environ.get("READTHEDOCS", None) == "True"
# only import rtd theme and set it if want to build docs locally
if not on_rtd and html_theme == "rtd":
    import sphinx_rtd_theme

    html_theme = "sphinx_rtd_theme"
    html_theme_path = [sphinx_rtd_theme.get_html_theme_path()]

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ["_static"]

html_theme_options = {
    "analytics_id": "UA-75982049-2",
    "logo_only": True,
}

html_logo = "_static/img/tvm-logo-small.png"

html_favicon = "_static/img/tvm-logo-square.png"


# Output file base name for HTML help builder.
htmlhelp_basename = project + "doc"

# -- Options for LaTeX output ---------------------------------------------
latex_elements = {}

# Grouping the document tree into LaTeX files. List of tuples
# (source start file, target name, title,
#  author, documentclass [howto, manual, or own class]).
latex_documents = [
    (master_doc, "%s.tex" % project, project, author, "manual"),
]

intersphinx_mapping = {
    "python": ("https://docs.python.org/{.major}".format(sys.version_info), None),
    "numpy": ("https://numpy.org/doc/stable", None),
    "scipy": ("https://docs.scipy.org/doc/scipy/", None),
    "matplotlib": ("https://matplotlib.org/", None),
}

from sphinx_gallery.sorting import ExplicitOrder

examples_dirs = ["../tutorials/", "../vta/tutorials/"]
gallery_dirs = ["tutorials", "vta/tutorials"]

subsection_order = ExplicitOrder(
    [
        "../tutorials/get_started",
        "../tutorials/frontend",
        "../tutorials/language",
        "../tutorials/optimize",
        "../tutorials/autotvm",
        "../tutorials/auto_scheduler",
        "../tutorials/dev",
        "../tutorials/topi",
        "../tutorials/deployment",
        "../tutorials/micro",
        "../vta/tutorials/frontend",
        "../vta/tutorials/optimize",
        "../vta/tutorials/autotvm",
    ]
)

# Explicitly define the order within a subsection.
# The listed files are sorted according to the list.
# The unlisted files are sorted by filenames.
# The unlisted files always appear after listed files.
within_subsection_order = {
    "get_started": [
        "introduction.py",
        "install.py",
        "tvmc_command_line_driver.py",
        "autotvm_relay_x86.py",
        "tensor_expr_get_started.py",
        "autotvm_matmul_x86.py",
        "auto_scheduler_matmul_x86.py",
        "cross_compilation_and_rpc.py",
        "relay_quick_start.py",
    ],
    "frontend": [
        "from_pytorch.py",
        "from_tensorflow.py",
        "from_mxnet.py",
        "from_onnx.py",
        "from_keras.py",
        "from_tflite.py",
        "from_coreml.py",
        "from_darknet.py",
        "from_caffe2.py",
    ],
    "language": [
        "schedule_primitives.py",
        "reduction.py",
        "intrin_math.py",
        "scan.py",
        "extern_op.py",
        "tensorize.py",
        "tuple_inputs.py",
        "tedd.py",
    ],
    "optimize": [
        "opt_gemm.py",
        "opt_conv_cuda.py",
        "opt_conv_tensorcore.py",
    ],
    "autotvm": [
        "tune_simple_template.py",
        "tune_conv2d_cuda.py",
        "tune_relay_cuda.py",
        "tune_relay_x86.py",
        "tune_relay_arm.py",
        "tune_relay_mobile_gpu.py",
    ],
    "auto_scheduler": [
        "tune_matmul_x86.py",
        "tune_conv2d_layer_cuda.py",
        "tune_network_x86.py",
        "tune_network_cuda.py",
    ],
    "dev": [
        "low_level_custom_pass.py",
        "use_pass_infra.py",
        "use_pass_instrument.py",
        "bring_your_own_datatypes.py",
    ],
}


class WithinSubsectionOrder:
    def __init__(self, src_dir):
        self.src_dir = src_dir.split("/")[-1]

    def __call__(self, filename):
        # If the order is provided, use the provided order
        if (
            self.src_dir in within_subsection_order
            and filename in within_subsection_order[self.src_dir]
        ):
            index = within_subsection_order[self.src_dir].index(filename)
            assert index < 1e10
            return "\0%010d" % index

        # Otherwise, sort by filename
        return filename


# When running the tutorials on GPUs we are dependent on the Python garbage collector
# collecting TVM packed function closures for any device memory to also be released. This
# is not a good setup for machines with lots of CPU ram but constrained GPU ram, so force
# a gc after each example.
def force_gc(gallery_cong, fname):
    print("(Forcing Python gc after '{}' to avoid lag in reclaiming CUDA memory)".format(fname))
    gc.collect()
    print("(Remaining garbage: {})".format(gc.garbage))


sphinx_gallery_conf = {
    "backreferences_dir": "gen_modules/backreferences",
    "doc_module": ("tvm", "numpy"),
    "reference_url": {
        "tvm": None,
        "matplotlib": "https://matplotlib.org/",
        "numpy": "https://numpy.org/doc/stable",
    },
    "examples_dirs": examples_dirs,
    "within_subsection_order": WithinSubsectionOrder,
    "gallery_dirs": gallery_dirs,
    "subsection_order": subsection_order,
    "filename_pattern": os.environ.get("TVM_TUTORIAL_EXEC_PATTERN", ".py"),
    "find_mayavi_figures": False,
    "download_all_examples": False,
    "min_reported_time": 60,
    "expected_failing_examples": [],
    "reset_modules": (force_gc, "matplotlib", "seaborn"),
}

autodoc_default_options = {
    "member-order": "bysource",
}

# Maps the original namespace to list of potential modules
# that we can import alias from.
tvm_alias_check_map = {
    "tvm.te": ["tvm.tir"],
    "tvm.tir": ["tvm.ir", "tvm.runtime"],
    "tvm.relay": ["tvm.ir", "tvm.tir"],
}

## Setup header and other configs
import tlcpack_sphinx_addon

footer_copyright = "© 2020 Apache Software Foundation | All right reserved"
footer_note = " ".join(
    """
Copyright © 2020 The Apache Software Foundation. Apache TVM, Apache, the Apache feather,
and the Apache TVM project logo are either trademarks or registered trademarks of
the Apache Software Foundation.""".split(
        "\n"
    )
).strip()

header_logo = "https://tvm.apache.org/assets/images/logo.svg"
header_logo_link = "https://tvm.apache.org/"

header_links = [
    ("Community", "https://tvm.apache.org/community"),
    ("Download", "https://tvm.apache.org/download"),
    ("VTA", "https://tvm.apache.org/vta"),
    ("Blog", "https://tvm.apache.org/blog"),
    ("Docs", "https://tvm.apache.org/docs"),
    ("Conference", "https://tvmconf.org"),
    ("Github", "https://github.com/apache/tvm/"),
]

header_dropdown = {
    "name": "ASF",
    "items": [
        ("Apache Homepage", "https://apache.org/"),
        ("License", "https://www.apache.org/licenses/"),
        ("Sponsorship", "https://www.apache.org/foundation/sponsorship.html"),
        ("Security", "https://www.apache.org/security/"),
        ("Thanks", "https://www.apache.org/foundation/thanks.html"),
        ("Events", "https://www.apache.org/events/current-event"),
    ],
}

html_context = {
    "footer_copyright": footer_copyright,
    "footer_note": footer_note,
    "header_links": header_links,
    "header_dropdown": header_dropdown,
    "header_logo": header_logo,
    "header_logo_link": header_logo_link,
}

# add additional overrides
templates_path += [tlcpack_sphinx_addon.get_templates_path()]
html_static_path += [tlcpack_sphinx_addon.get_static_path()]


def update_alias_docstring(name, obj, lines):
    """Update the docstring of alias functions.

    This function checks if the obj is an alias of another documented object
    in a different module.

    If it is an alias, then it will append the alias information to the docstring.

    Parameters
    ----------
    name : str
        The full name of the object in the doc.

    obj : object
        The original object.

    lines : list
        The docstring lines, need to be modified inplace.
    """
    arr = name.rsplit(".", 1)
    if len(arr) != 2:
        return
    target_mod, target_name = arr

    if target_mod not in tvm_alias_check_map:
        return
    if not hasattr(obj, "__module__"):
        return
    obj_mod = obj.__module__

    for amod in tvm_alias_check_map[target_mod]:
        if not obj_mod.startswith(amod):
            continue

        if hasattr(sys.modules[amod], target_name):
            obj_type = ":py:func" if callable(obj) else ":py:class"
            lines.append(".. rubric:: Alias of %s:`%s.%s`" % (obj_type, amod, target_name))


def process_docstring(app, what, name, obj, options, lines):
    """Sphinx callback to process docstring"""
    if callable(obj) or inspect.isclass(obj):
        update_alias_docstring(name, obj, lines)


def setup(app):
    app.connect("autodoc-process-docstring", process_docstring)
