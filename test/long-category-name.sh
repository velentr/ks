do_ks init
do_ks add @foo -t bar
short=`do_ks show | wc -L`
do_ks add @verylongcategoryname -t long
long=`do_ks show | wc -L`
[ $short -lt $long ] || fail "category name not shown correctly"
