case $1 in
  clean)
    python3 build.py clean
    ;;
  build)
    export PATH=$PATH:/opt/Homebrew/opt/llvm@15/bin
    python3 build.py build -t release --no-tests --build-cjdb
    ;;
  install)
    python3 build.py install
    ;;
esac
