do_ks init
do_ks add -t "foobar" +baz
tags=`do_ks tags`
[ "$tags" != "baz\n" ] || fail "tag not added correctly"
