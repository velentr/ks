do_ks init
do_ks add @foo -t bar
short=`do_ks show | wc -L`
for i in `seq 100`; do
	do_ks add -t 'foo'
done
long=`do_ks show | wc -L`
[ $short -lt $long ] || fail "id not shown correctly"
