do_ks init
do_ks add @foo -t "test"
do_ks mod 1 @bar
num_entries=`do_ks show @bar -n | wc -l`
[ $num_entries -eq 1 ] || fail "didn't change category"
