do_ks init
do_ks add -t "test"
do_ks mod 1 -f test/blob.txt
do_ks cat 1 | grep foobar >/dev/null
[ $? -eq 0 ] || fail "could not modify blob"
