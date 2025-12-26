cxx := g++
cxxflags := -std=c++17 -O2 -Wall -Wextra

target := imgsort

src := imgsort.cpp

all: $(target)

$(target): $(src)
	$(cxx) $(cxxflags) $(src) -o $(target)

clean:
	rm -f $(target)

rebuild: clean all

.PHONY: all clean rebuild

