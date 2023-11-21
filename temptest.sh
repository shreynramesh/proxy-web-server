for i in {1..20}
do
	curl 'http://localhost:8889/2/dummy1.html' 
	echo finished request 1
	curl 'http://localhost:8890/1/dummy1.html'
	echo finished request 2
done
