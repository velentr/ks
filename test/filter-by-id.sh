do_ks init
do_ks add -t foobar
do_ks show -n 1 | grep foobar >/dev/null
[ $? -eq 0 ] || fail "couldn't filter by id"
