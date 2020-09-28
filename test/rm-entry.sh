do_ks init
do_ks add --title "foobar"
do_ks rm 1
num_entries=`do_ks show -n | wc -l`
[ $num_entries -eq 0 ] || fail "didn't remove entry"
