#include <iostream>
#include <vector>
#include <string>

#include  "load_files/getfiles.hpp"
#include "../CLIP/CLIP_image.cpp"
#include "../CLIP/CLIP_model.cpp"


using namespace std;
 


int main(){
    vector<string> file_paths;
    cout << "Starting to acces files ...\n";

    // Now we have the file paths of all the files we want to index or parse
    file_paths = get_file_path("./data");
     
    cout << "Starting Parsing ...\n";

    // We will change it to all files afterwards
    string path = file_paths[0];
    cout<< path << endl;

    CLIP_instance image(path);

    CLIP_session image_encoder;
    image_encoder.load();
    
    auto embedding = image_encoder.get_embedding(image);

    for(auto &a: embedding) 
        cout<< a << " ";
    cout<<endl;

    image_encoder.unload();

    return 0;
}