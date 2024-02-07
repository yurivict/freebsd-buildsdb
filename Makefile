# Copyright (C) 2024 by Yuri Victorovich. All rights reserved.


CXXFLAGS?=	-O2
CXXFLAGS+=	-std=c++20
CXXFLAGS+=	-Wall

SRC=		main.cpp schema.cpp

all: buildsdb

buildsdb: ${SRC}
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ ${SRC} `pkg-config --cflags --libs libcurl nlohmann_json` -lSQLiteCpp -pthread

install:
	install buildsdb $(DESTDIR)$(PREFIX)/bin
