do_ks init
do_ks add -t "foobar" +baz
do_ks show -n | grep baz >/dev/null
[ $? -eq 0 ] || fail "tag not added correctly"
