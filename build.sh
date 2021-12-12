#!/bin/sh
pandoc -f gfm -s -c light.min.css README.md -o index.html
