#!/bin/bash
rm *.txt *.err *.out -rf Quic_artifacts Tcp_artifacts
python3 ./waf build