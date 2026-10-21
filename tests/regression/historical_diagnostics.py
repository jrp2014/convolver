#!/usr/bin/env python3
"""Test recovered stream-error classification against real iostream states.

Extract only the ordered bad/fail clauses; parsing and Windows I/O are not run.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision')
    args = parser.parse_args()
    methods = []
    for index, name in enumerate(('channelpaths.cpp', 'convolution.cpp')):
        relative = 'historical/convolution/' + name
        if args.revision:
            # The historical tree was called convolver/ before 21 October 2026.
            shown = subprocess.run(['git', 'show', args.revision + ':' + relative], cwd=ROOT, capture_output=True)
            if shown.returncode != 0:
                shown = subprocess.run(['git', 'show', args.revision + ':convolver/convolution/' + name],
                                       cwd=ROOT, capture_output=True, check=True)
            text = shown.stdout.decode()
        else:
            text = (ROOT / relative).read_text()
        pattern = r'else if \(config_?\(\)\.(?:fail|bad)\(\)\)\s*\{.*?\}\s*else if \(config_?\(\)\.(?:fail|bad)\(\)\)\s*\{.*?\}'
        clauses = re.search(pattern, text, re.S).group()
        clauses = clauses.replace('config_()', 'stream').replace('config()', 'stream')
        clauses = re.sub(r'throw (?:channelPaths|convolutionList)Exception\("([^"]+)", \w+\);', r'return "\1";', clauses)
        methods.append('std::string classify' + str(index) + '(std::istream& stream) { if(false) {} ' + clauses + ' return "ok"; }')
    code = '#include <sstream>\n#include <iostream>\n#include <string>\n' + '\n'.join(methods) + r'''
int main() {
    int count=0;
    for(auto classify : {classify0, classify1}) {
        for(auto state : {std::ios::goodbit, std::ios::failbit, std::ios::badbit,
                          std::ios::iostate(std::ios::badbit | std::ios::failbit)}) {
            std::istringstream stream;
            stream.setstate(state);
            const auto got=classify(stream);
            const auto expected=(state & std::ios::badbit) ? "Fatal" : (state & std::ios::failbit) ? "Bad" : "ok";
            if(got.find(expected)!=0) {std::cerr<<got<<" expected "<<expected<<'\n'; return 1;}
            ++count;
        }
    }
    std::cout<<"PASS "<<count<<" historical diagnostic cases\n";
}
'''
    with tempfile.TemporaryDirectory(prefix='convolver-diagnostics-') as tmp:
        cpp = Path(tmp) / 'test.cpp'; exe = Path(tmp) / 'test'
        cpp.write_text(code)
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++11', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(cpp), '-o', str(exe)], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
