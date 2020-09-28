do_ks init
do_ks add -t "foo"
do_ks mod 1 -t "bar"
do_ks show | grep foo >/dev/null
[ $? -ne 0 ] || fail "didn't change title"
do_ks show | grep bar >/dev/null
[ $? -eq 0 ] || fail "title changed incorrectly"
