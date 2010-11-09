#!/bin/sh

autoheader
aclocal
autoconf
automake --add-missing

