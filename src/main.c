#include <stdio.h>
#include <sqlite3.h>

#include "core/language_layer.h"
#include "core/mem.h"
#include "core/str.h"
#include "core/sql.h"
#include "core/hash_table.h"

#define MAX_NAME_LENGTH 32
#define MAX_N_EXERCISES 32
#define MAX_N_SETS 32 

typedef struct Exercise {
    char name[MAX_NAME_LENGTH];
    u32 n_sets;
    u32 curr_rep_idx;
} Exercise;

Exercise exercise_create(char* exercise_name, u32 n_sets) {
    Exercise exercise = {0};
    MemoryCopy(exercise.name, exercise_name, strlen(exercise_name));
    exercise.n_sets = n_sets;
    exercise.curr_rep_idx = 0;
    return exercise;
}

typedef struct Workout {
    char name[MAX_NAME_LENGTH];

    char exercise_names[MAX_N_EXERCISES][MAX_NAME_LENGTH];
    u32 set_counts[MAX_N_EXERCISES];
    u32 rep_offsets[MAX_N_EXERCISES];
    u32 curr_rep_idxs[MAX_N_EXERCISES];
    u32 reps[MAX_N_SETS];

    u32 n_exercises;
    u32 curr_rep_offset;
} Workout;

Workout workout_create(char* workout_name) {
    Workout workout = {0};
    MemoryCopy(workout.name, workout_name, strlen(workout_name));
    return workout;
}

u32 workout_add_exercise(Workout* workout, Exercise exercise) {
    u32 exercise_idx = workout->n_exercises++;

    MemoryCopy(workout->exercise_names[exercise_idx], exercise.name, strlen(exercise.name));
    workout->set_counts[exercise_idx] = exercise.n_sets;
    workout->rep_offsets[exercise_idx] = workout->curr_rep_offset;
    workout->curr_rep_idxs[exercise_idx] = 0;

    workout->curr_rep_offset += exercise.n_sets;
    return exercise_idx;
}

u32 workout_get_reps(Workout* workout, u32 exercise_idx, u32 set) {
   return workout->reps[set + workout->rep_offsets[exercise_idx]];
}

void workout_complete_set(Workout* workout, u32 exercise_idx, u32 reps) {
    u32 rep_idx = workout->curr_rep_idxs[exercise_idx]++;
    u32 rep_offset = workout->rep_offsets[exercise_idx];
    workout->reps[rep_idx + rep_offset] = reps;
}

void workout_print(Workout* workout) {
    printf("%s, %d Exercises:\n", workout->name, workout->n_exercises);
    for (i32 exercise_idx = 0; exercise_idx < workout->n_exercises; ++exercise_idx) {
        printf("\t%s:\n", workout->exercise_names[exercise_idx]);
        for (i32 set = 0; set < workout->set_counts[exercise_idx]; ++set) {
            printf("\t\tSet %d: %d reps\n", set+1, workout_get_reps(workout, exercise_idx, set));
        } 
    }
}

void workout_save(Workout* workout, Arena* arena) {
    TempArena tmp = temp_arena_begin(arena);
    SQLDB db = sql_db_create("workout.db");
    
    SQLCommandBuffer cmd_buff = sql_command_buffer_begin(arena);
    sql_command_buffer_push(&cmd_buff, "DROP TABLE IF EXISTS Workouts;");
    sql_command_buffer_push(&cmd_buff, "CREATE TABLE Workouts(workout_name TEXT, exercise_name TEXT, reps INT);");
    
    for (i32 exercise_idx = 0; exercise_idx < workout->n_exercises; ++exercise_idx) {
        for (i32 set = 0; set < workout->set_counts[exercise_idx]; ++set) {
            char fmt[512];
            sprintf(fmt, "INSERT INTO Workouts VALUES('%s', '%s', %d);", workout->name, workout->exercise_names[exercise_idx], workout_get_reps(workout, exercise_idx, set));
            sql_command_buffer_push(&cmd_buff, fmt);
        } 
    }

    SQLCommand cmd = sql_command_buffer_end(&cmd_buff);
    sql_db_submit(&db, &cmd);

    sql_db_close(&db);
    temp_arena_end(&tmp);
}

Workout* workout_load_multiple() {
    Workout* workouts = NULL;
    return workouts;
}

Workout workout_load(Arena* arena, char* filename, b32 complete) {
    TempArena tmp = temp_arena_begin(arena);
    Workout workout = {0};

    HashTable ht = hash_table_create(arena, 256);

    typedef struct Tracker{
        Exercise e;
        u32 rep_counts[MAX_N_SETS];
    } Tracker;

    u32 n_trackers = 0;
    Tracker trackers[MAX_N_EXERCISES];

    SQLDB db = sql_db_create(filename);

    SQLCommand name_cmd = sql_command_create (arena, "SELECT DISTINCT workout_name FROM Workouts;");
    sql_db_prepare(&db, &name_cmd);
    sql_db_step(&db);
    char* workout_name  = (char*) sqlite3_column_text(db.res, 0);
    MemoryCopy(workout.name, workout_name, strlen(workout_name));

    SQLCommand exercise_cmd = sql_command_create (arena, "SELECT exercise_name, reps FROM Workouts;");
    sql_db_prepare(&db, &exercise_cmd);
    while (sql_db_step(&db) == SQLITE_ROW) {
        char* exercise_name  = (char*) sqlite3_column_text(db.res, 0);
        i32 rep_count  = sqlite3_column_int(db.res, 1);
        i32* tracker_id = (i32*) hash_table_get(&ht, exercise_name);

        if (tracker_id == NULL) {
            tracker_id = arena_push(arena, i32);
            *tracker_id = n_trackers++;
            trackers[*tracker_id].e = exercise_create(exercise_name, 0);
            hash_table_insert(&ht, exercise_name, (byte*) tracker_id);
        }
        
        Tracker* t = &trackers[*tracker_id];
        t->e.n_sets++;
        t->rep_counts[t->e.curr_rep_idx++] = rep_count;
    }

    for (u32 i = 0; i < n_trackers; i++) {
        u32 exercise_idx = workout_add_exercise(&workout, trackers[i].e);
        if (complete) {
            for (u32 j = 0; j < trackers[i].e.n_sets; j++)  {
                    workout_complete_set(&workout, exercise_idx, trackers[i].rep_counts[j]);
            }
        }
    }

    sqlite3_finalize(db.res);
    sql_db_close(&db);
    temp_arena_end(&tmp);
    return workout;
}

int main (void) {
    Arena arena = arena_create(Megabytes(4));

    Workout push_day = workout_create("Push Day");

    u32 bench_idx = workout_add_exercise(&push_day, exercise_create("Bench Press", 4));
    u32 ohp_idx = workout_add_exercise(&push_day, exercise_create("Overhead Press", 2));

    workout_complete_set(&push_day, bench_idx, 5);
    workout_complete_set(&push_day, ohp_idx, 10);
    workout_complete_set(&push_day, bench_idx, 3);
    workout_complete_set(&push_day, ohp_idx, 6);

    workout_print(&push_day);

    workout_save(&push_day, &arena);
    workout_load(&arena, "workout.db", true);

    arena_release(&arena);
    return 0;
}
