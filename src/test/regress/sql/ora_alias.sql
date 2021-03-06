--
-- table aliasname for grammar = oracle
-- Solution bug: http://192.168.11.45:8000/issues/24013
--
set grammar to oracle;

-- create test table
create table t (id int,name varchar(22), rate int check(rate is not null));

-- scene 1: 插入表名，字段为表别名.列名
insert into  t a (a.id,name,a.rate) values (1,'aa',2);
insert into  t a (a.rate) values (2); 
select * from t ;

-- scene 2:select 简单查询带alias
select * from t a ;
select * from t a where a.id is null;
select * from t a where a.id is null and rate is not null; 

-- scene 3: update 带alias
update t a set a.name=4 where a.id is null and rate is not null;  
select * from t a;

-- scene 4:union 联合查询
select * from t a where id is not null
union all
select * from t a where a.id is null and rate is not null;

-- scene 5: update 字段部分带alias，部分字段不带alias
update t a set a.name=2,rate='13' where a.id='' and name=4;
update t a set a.name=2,rate='13' where a.id is null  and name=4;    
select * from t ;

-- init table/data
drop table t ;
