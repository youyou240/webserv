#!/usr/bin/env python3
import os
import sys

print("Content-Type: text/plain")
print()
print("Hello from a Python CGI!")
print("REQUEST_METHOD =", os.environ.get("REQUEST_METHOD", "(unset)"))
print("QUERY_STRING   =", os.environ.get("QUERY_STRING", "(unset)"))
if os.environ.get("REQUEST_METHOD") == "POST":
    print("BODY =", sys.stdin.read())
