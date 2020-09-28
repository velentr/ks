do_ks init
do_ks add @foo -t "test"
do_ks add @bar -t "test"
num_entries=`do_ks show @foo -n | wc -l`
[ $num_entries -eq 1 ] || fail "couldn't filter by category"
do_ks show @foo | grep foo >/dev/null
[ $? -eq 0 ] || fail "incorrectly filtered by category"
