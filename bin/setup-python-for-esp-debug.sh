# shellcheck shell=bash
# (this minor script is actually shell agnostic, and is intended to be sourced rather than run in a subshell)

# This is a little script you can source if you want to make ESP debugging work on a modern (24.04) ubuntu machine
# It assumes you have built and installed python 2.7 from source with:
# ./configure --enable-optimizations --enable-shared --enable-unicode=ucs4
# sudo make clean
# make
# sudo make altinstall

export LD_LIBRARY_PATH=$HOME/packages/python-2.7.18/
export PYTHON_HOME=/usr/local/lib/python2.7/
