#!/usr/bin/env bash

compiler_kind="$1"
runner_os="$2"
target_abi="$3"
target_system_name="$4"
target_arch="$5"

set -e

if [[ -z "$GITHUB_OUTPUT" ]]; then
  echo "Error: This script should only be run in github actions environment"
  exit 1
fi
if [[ -z "${runner_os}" || -z "${target_abi}" || -z  "${target_arch}" ]]; then
  echo "Error: Not all required parameters where set"
  exit 1
fi
if [[ -z "${compiler_kind}" || "${compiler_kind}" == "default" ]]; then
  echo "compiler option was not set. Determining default compiler."
  if [[ "${runner_os}" == "Windows" ]]; then
    if [[ "${target_abi}" == "msvc" ]]; then
      compiler_kind=msvc
    elif [[ "${target_abi}" == "gnu" ]]; then
      compiler_kind=gcc
    else
      echo "Unknown abi for Windows: ${target_abi}"
      exit 1
    fi
  elif [[ "${runner_os}" == "macOS" ]]; then
    compiler_kind="clang"
  elif [[ "${runner_os}" == "Linux" ]]; then
    compiler_kind="gcc"
  else
    echo "Unknown Runner OS: ${runner_os}"
    exit 1
  fi
fi
echo "Compiler Family: '${compiler_kind}'"

if [[ "${compiler_kind}" == "clang" ]]; then
  c_compiler="clang"
  cxx_compiler="clang++"
elif [[ "${compiler_kind}" == "msvc" ]]; then
  c_compiler="cl"
  cxx_compiler="cl"
elif [[ "${compiler_kind}" == "gcc" ]]; then
  if [[ -z "${target_system_name}" ]]; then
    c_compiler="gcc"
    cxx_compiler="g++"
  else
    c_compiler="${target_arch}-linux-gnu-gcc"
    cxx_compiler="${target_arch}-linux-gnu-g++"
  fi
fi
echo "Chose C compiler: '${c_compiler}'"
echo "Chose C++ compiler: '${cxx_compiler}'"
echo "c_compiler=-DCMAKE_C_COMPILER=${c_compiler}" >> $GITHUB_OUTPUT
echo "cxx_compiler=-DCMAKE_CXX_COMPILER=${cxx_compiler}" >> $GITHUB_OUTPUT
