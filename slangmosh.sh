#!/bin/bash
set -euo pipefail

slangmosh --output shaders/slangmosh.hpp shaders/slangmosh.json --namespace PyroWave -O --strip
slangmosh --output shaders/slangmosh_scaler.hpp third_party/Granite/video/slangmosh_encode.json --namespace PyroWave -O --strip
