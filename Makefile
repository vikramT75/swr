# Makefile for SWR

.PHONY: all configure build run debug clean

all: run

configure:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build build -j

run: build
	./build/swr

debug:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
	cmake --build build -j
	./build/swr

clean:
	rm -rf build
