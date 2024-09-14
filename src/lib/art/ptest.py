# https://ep2016.europython.eu/media/conference/slides/writing-unit-tests-for-c-code-in-python.html
import cffi
import uuid
import re
import subprocess
import pycparser
import pycparser.c_generator
import importlib

def preprocess(source):
    return subprocess.run(['gcc', '-E', '-P', '-I/home/matt/src/compilers/pycparser/utils/fake_libc_include', '-'],
                          input=source, stdout=subprocess.PIPE,
                          universal_newlines=True, check=True).stdout

def load(filename):
    name = filename + '_' + uuid.uuid4().hex

    source = open(filename + '.c').read()
    # preprocess all header files for CFFI
    includes = preprocess(''.join(re.findall('\s*#include\s+.*', source)))

    # prefix external functions with extern "Python+C"
    p = preprocess(source)
    with open ("source.i", "w") as f:
        f.write(p)

    local_functions = FunctionList(p).funcs
    includes = convert_function_declarations(includes, local_functions)

    ffibuilder = cffi.FFI()
    ffibuilder.set_source(name, source)
    ffibuilder.cdef(includes)
    ffibuilder.compile(verbose=True)

    module = importlib.import_module(name)
    # return both the library object and the ffi object
    return module.lib, module.ffi

class FunctionList(pycparser.c_ast.NodeVisitor):
    def __init__(self, source):
        self.funcs = set()
        self.visit(pycparser.CParser().parse(source))

    def visit_FuncDef(self, node):
        self.funcs.add(node.decl.name)

class CFFIGenerator(pycparser.c_generator.CGenerator):
    def __init__(self, blacklist):
        super().__init__()
        self.blacklist = blacklist

    def visit_Decl(self, n, *args, **kwargs):
        result = super().visit_Decl(n, *args, **kwargs)
        if isinstance(n.type, pycparser.c_ast.FuncDecl):
            if n.name not in self.blacklist:
                return 'extern "Python+C" ' + result
        return result

def convert_function_declarations(source, blacklist):
    return CFFIGenerator(blacklist).visit(pycparser.CParser().parse(source))
