# LibFinder

**Problem**
You include some system header, compile and you get `undefined reference to X` and now need to hunt down the library you need to link.

**Solution**
You run `sudo updatedb` to update your file location database, then `libfinder -u` to update the lookup table and then `libfinder -s X` and libfinder will tell you which libraries define the symbol you need.

**Further options**
Output of `libfinder -h`:

    libfinder finds the libraries that define a given symbol.
    Run 'sudo updatedb' to make sure all libs are locatable, create an index with
    'libfinder -u' (once every time your libs change) and look up a symbol with
    'libfinder -s [symbol]' to get a list of libraries that define [symbol].
    Parameters:
      -h [ --help ]              print this
      -u [ --update ] [=arg(=8)] update lookup table (must be done before first
                                 use) with given number of threads (default=8)
      -s [ --symbol ] arg        the symbol to look up

**Future work**
Add an option to instead of printing the file path, print either the linker flags or makefile line or CMakeLists.txt entry or qmake file entry to link that library.
