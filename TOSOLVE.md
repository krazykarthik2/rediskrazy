
sql> select * from tbl1
Error: ERR unknown command

sql> select * from tbl1;
Error: ERR unknown command

sql> select * from tbl1 where key=''
Error: Expected 'key' in WHERE clause
sql> select * from tbl1 where       
Error: Expected 'key' in WHERE clause
sql> select * from tbl1 where true 
Error: Expected 'key' in WHERE clause
sql> select * from tbl1 where 1==1
Error: Expected 'key' in WHERE clause