TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
CONFIG += c++14

SOURCES += main.cpp \
    lookup.cpp \
    generate.cpp \
    utility.cpp \
    test.cpp \
    argument_parser.cpp \
    asserts.cpp \
    radix_tree.cpp \
    test_radix_tree.cpp

HEADERS += \
    thread_safe_queue.h \
    lookup.h \
    main.h \
    generate.h \
    utility.h \
    test.h \
    argument_parser.h \
    gsl-lite.h \
    asserts.h \
    radix_tree.h \
    test_radix_tree.h

LIBS += -lpthread
LIBS += -lboost_system
LIBS += -lboost_filesystem
LIBS += -lboost_program_options

QMAKE_CXXFLAGS += -std=c++1z
QMAKE_CXXFLAGS_DEBUG += -O0 -fno-omit-frame-pointer -Wall -Werror -Wfatal-errors
linux-clang{
    QMAKE_CXXFLAGS_DEBUG += -Wthread-safety -fsanitize=undefined,address#,safe-stack
    QMAKE_LFLAGS_DEBUG += -fsanitize=undefined,address#,safe-stack
}
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
