do_ks init
do_ks add @foo -t bar
short=`do_ks show | wc -L`
do_ks add -t 'Very long title'
long=`do_ks show | wc -L`
[ $short -lt $long ] || fail "title not shown correctly"
