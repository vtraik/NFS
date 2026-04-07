# NetworkFileSystem-NFS

- **compile**: make all στο root directory.Τα εκτελέσιμα τρέχουν και αυτά από το root: 
./bin/nfs_manager... ή ./bin/nfs_console... . Η σειρά εκτέλεσης είναι: client, manager, console.

- Τα modules (Mod*.cpp) περιέχουν συναρτήσεις init,destroy για τις οντότητες manager,
console και άλλες που ρυθμίζουν το αρχικό connection και τους handlers. 

- Τα log/config πηγαίνουν στο log directory (σύμβαση, μπορούν και αλλού απλά για να υπάρχει μια οργάνωση το έχω).

- Όλα τα fd's είναι blocking (sockets/files).

- Όλη η επικοινωνία με sockets γίνεται μέσω μηνυμάτων (read_message/write message).
Ένα μήνυμα αποτελείται από το prefix (που δηλώνει το μέγεθός του) και το data μέρος
(το ίδιο το μήνυμα).
Οι εντολές LIST,PULL,PUSH στέλνονται ως: 
Έστω message(X) := X r/w με prefix, data(X) := X r/w με chunk
- message(List dir) και η απάντηση: message(file + \n) , message(.) για signal τέλους.
- message(PULL file) και η απάντηση: message(filesize + " ") , data(file.data).
- message(PUSH file chunck_size) και data(file.data).

- Όποτε κάποιος γράφει πάνω σε socket και πρέπει να κλείσει η σύνδεση μετά, πριν την close
στέλνει shutdown(fd,SHUT_WR).

- Άδεια dirs δεν συγχρονίζονται.

- Utils: περιέχονται οι συναρτήσεις για read/write καθώς και άλλες χρήσιμες (για την
επικοινωνία μέσω socket, για parsing κτλ). 

- Δομές: η ουρά μεγέθους buf_size και ένα map που φυλάει εγγραφές (όπως αυτές ορίζονται
στο config file) και ελέγχει αν υπάρχει στην ουρά η εγγραφή στην add. Η κάθε εγγραφή έχει
έναν counter που μετρά πόσες φορές υπάρχει στην ουρά (πχ για τον συγχρονισμό 
/test@195.134.65.86:16420 /backup@127.0.0.1:16421 αρχικά θα είναι όσα είναι τα αρχεία του
test directory, όταν βγαίνει κάποια δουλειά από τον buffer μειώνεται κτλ και αν γίνει 0
βγαίνει τελείως από το map). Οι δομές είναι κοινές για όλα τα νήματα.

## NfsManager
Μετά την αρχικοποίησή του και του thread pool καλεί τη config_files, η οποία αναλαμβάνει
τον αρχικό συγχρονισμό όπως περιγράφεται στην εκφώνηση. Η list_command επικοινωνεί με
τον client διαβάζοντας τα αρχεία του dircetory και τα βάζει στην ουρά. Έπειτα δημιουργείται
το console thread που θα αναλάβει την επικοινωνία με το console. Το main thread περιμένει
να κάνει join με αυτό όταν τερματίσει σε shutdown.

### Console thread (handle_console)
Διαβάζει τις εντολές του console και καλεί την αντίστοιχη συνάρτηση.
- add: Ψάχνει στο map Jobs_record αν υπάρχει η εγγραφή. Αν δεν υπάρχει
καλεί την list_command για να ξεκινήσει τον συγχρονισμό του dir.
- cancel: Ψάχνει για entry όπως: /testadd@195.134.65.86:16420 στην ουρά
Jobs και τα διαγράφει, όπως και από το map Jobs_record. Αν βρει πολλά
κάνει broadcast στο cond_var που κοιμούνται οι workers, αλλιώς signal ή
τίποτα.
- shutdown: Κάνει τη global μεταβλητή exit_nfs = true και ξυπνά όσα worker threads
κοιμούνται και περιμένει να τα κάνει join, κάνει destroy τα mutexes και cond_var
και κάνει exit ώστε να επιστρέψει η join του main thread.


### Worker threads (worker)
Γίνεται ο συγχρονισμός με τους απαραίτητους ελέγχους και στο τέλος γράφεται η εγγραφή ή 
το error στο logfile που προστατεύεται από mutex. Αν υπάρξει error κλείνει και η σύνδεση
ομαλά (καλώντας shutdown πριν το close). Ο συγχρονισμός βασίζεται στο μέγεθος της ουράς,
όπως και αν έχει ζητηθεί ο τερματισμός της εφαρμογής από το console. 

## NfsClient
**Παραδοχή για τον συγχρονισμό**: Δεν υπάρχουν ταυτόχρονα 2 writes στο ίδιο αρχείο.

- Για κάθε request φτιάχνεται ένα thread.
- ανοίγει relative paths (./dir) αν υπάρχουν.

Συγχρονισμός: read ή write lock ανά dir. Οι αντίστοιχες συναρτήσεις get_dir_lock,
start/end reading/writing υλοποιούν τον συγχρονισμό αυτό (πολλαπλοί readers ή writers
ανά dir). Για να μπορώ να ελέγχω το lifetime ενός reference του map τα values του είναι
shared_ptr<dir_lock>. Έτσι κάθε thread παίρνει έναν shared_ptr για να κάνει το operation
που θέλει και όταν τελειώσει το απελευθερώνει. Όταν ένα dir_lock έχει ref count == 1,
σημαίνει ότι δεν χρησιμοποείται (υπάρχει μόνο στο map). Ένα cleanup thread είναι υπεύθυνο
να καθαρίζει αυτές τις εγγραφές από το map όταν το size του ξεπερνά τις 1000 εγγραφές
(για να μην κάνω allocate συνεχώς καινούργια μνήμη).

Δομή: ένα map που φυλάει τα dirs και τα lock τους. Το key του map είναι ο συνδυασμός
του dev_t + ino_t που επιστρέφει η stat. Επειδή χρειαζόταν να γίνουν combine τα δύο
keys χρησιμοποίησα τον τρόπο που το κάνει η boost library (η οποία δεν υπάρχει στα linux
οπότε βρήκα πώς κάνουν το combine για καλές στατιστικές ιδιότητες).

- PUSH: αν δεν διαβαστεί PUSH file 0 κάνει unlink το αρχείο (failed operation).

Ο client μπορεί να τερματίσει ομαλά μέσω του σήματος SIGINT. Για να επιτευχθεί αυτό,
ο handler κάνει set το flag exit_client σε 1 κάνει break από το accept loop και περιμένει
τους active workers να τερματίσουν (cur_worker == 0) και να επιστρέψει το cleanup thread.

## NfsConsole
Μετά την αρχικοποίηση, διαβάζει ένα command μέσω της getline και περιμένει το response.
- handle_input: Κάνει parse το input command και το στέλνει στον manager και γράφει στο
console log file.
- handle_response: Διαβάζει την απάντηση του manager. Αν είναι added ή shutting μπαίνει
σε loop για να διαβάσει τα υπόλοιπα responses που πρέπει (σε αυτές τις περιπτώσεις ο
manager στέλνει > 1 μηνύματα).
