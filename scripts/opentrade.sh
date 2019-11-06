#!/bin/bash
export LD_PRELOAD=libtbbmalloc_proxy.so
exec ./opentrade
