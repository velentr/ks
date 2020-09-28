do_ks init
do_ks add -t 'test'
do_ks mod 1 +foo
num_entries=`do_ks show -n +foo | wc -l`
[ $num_entries -eq 1 ] || fail "failed to add tag"
