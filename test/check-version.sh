do_ks version | grep 0.0.0 >/dev/null
[ $? -eq 0 ] || fail "incorrect version number"
