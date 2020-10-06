do_ks init
do_ks add @foo -t bar
short=`do_ks show | wc -L`
do_ks add -t long +verylongtagname
long=`do_ks show | wc -L`
[ $short -lt $long ] || fail "tag name not shown correctly"
