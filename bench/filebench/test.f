define fileset name="testF",entries=10,filesize=16k,prealloc,path="/mlfs"
define process name="readerP",instances=2 {
  thread name="readerT",instances=3 {
    flowop openfile name="openOP",filesetname="testF"
    flowop readwholefile name="readOP",filesetname="testF"
    flowop closefile name="closeOP"
  }
}
run 5
