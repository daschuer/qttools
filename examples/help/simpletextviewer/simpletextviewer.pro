HEADERS       = mainwindow.h \
                findfiledialog.h \
                assistant.h \
                textedit.h
SOURCES       = main.cpp \
                mainwindow.cpp \
                findfiledialog.cpp \
                assistant.cpp \
                textedit.cpp

QT += widgets

# install
target.path = $$[QT_INSTALL_EXAMPLES]/qttools/help/simpletextviewer
INSTALLS += target
