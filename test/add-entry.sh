do_ks init
do_ks add -t foobar
do_ks show -n | grep foobar >/dev/null
[ $? -eq 0 ] || fail "entry not found"
