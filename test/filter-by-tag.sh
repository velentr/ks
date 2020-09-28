do_ks init
do_ks add -t "test" +foo
do_ks add -t "test" +bar
num_entries=`do_ks show +foo -n | wc -l`
[ $num_entries -eq 1 ] || fail "couldn't filter by tag"
do_ks show +foo | grep foo >/dev/null
[ $? -eq 0 ] || fail "incorrectly filtered by tag"
