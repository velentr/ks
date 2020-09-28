do_ks init
num_entries=`do_ks show -n | wc -l`
[ $num_entries -eq 0 ] || fail "got $num_entries instead of 0"
