#!/bin/bash
rm *.txt *.err *.out -rf *_artifacts
python3 ./waf build