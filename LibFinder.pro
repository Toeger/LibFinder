TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
#CONFIG += c++26
CONFIG -= warn

SOURCES += main.cpp \
    generate.cpp \
    utility.cpp \
    test.cpp \
    asserts.cpp \
    symbol_database.cpp \
    libparser.cpp \
    symbol_matcher.cpp

HEADERS += \
    thread_safe_queue.h \
    main.h \
    generate.h \
    utility.h \
    test.h \
    asserts.h \
    symbol_database.h \
    libparser.h \
    external/nlohmann/json.hpp \
    symbol_matcher.h

LIBS += -lpthread
LIBS += -lboost_system
LIBS += -lboost_program_options

QMAKE_CXXFLAGS += -std=c++26
QMAKE_CXXFLAGS_DEBUG += -O0 -fno-omit-frame-pointer -Wall -Werror -fdiagnostics-color -Wold-style-cast -fdiagnostics-all-candidates #-Wfatal-errors
QMAKE_CXXFLAGS_DEBUG += -fsanitize=undefined,address#,safe-stack
QMAKE_LFLAGS_DEBUG += -fsanitize=undefined,address#,safe-stack
QMAKE_CXXFLAGS_PROFILE += -DNDEBUG
QMAKE_CXXFLAGS_RELEASE += -O3 -DNDEBUG
gcc{
    clang{
        #clang pretends to be gcc but doesn't support -flto
    }
    else{
        QMAKE_CXXFLAGS_RELEASE += -flto
        QMAKE_LFLAGS_RELEASE += -flto
    }
}

DISTFILES += \
    README.md
