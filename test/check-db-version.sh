do_ks init
do_ks version -D | grep -v 0.0.0 | grep 0 >/dev/null
[ $? -eq 0 ] || fail "didn't get database version"
