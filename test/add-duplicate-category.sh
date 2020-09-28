do_ks init
do_ks add @foo -t "foobar"
do_ks add @foo -t "baz"

num_categories=`do_ks categories | wc -l`
[ $num_categories -eq 1 ] || fail "got $num_categories categories instead of 1"
