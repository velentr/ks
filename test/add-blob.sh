do_ks init
do_ks add -t "test" -f test/blob.txt
do_ks cat 1 | grep foobar >/dev/null
[ $? -eq 0 ] || fail "incorrectly added blob"
