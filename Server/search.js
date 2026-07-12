function escapeRegExp(string){

    return string.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');

}
function highlight(text, keyword){

    if(!text){
        return text;
    }
    if(keyword==""){
        return text;
    }
    let reg = new RegExp(escapeRegExp(keyword), "g");
    return text.replace(reg, `<span class='highlight'>${keyword}</span>`);

}
let input=document.getElementById("search-input");
function search(newsList){
    let params = new URLSearchParams(window.location.search);
    let keyword = input.value || params.get("keyword");
    let result = newsList.filter(news=>{
        return(
        (news.title && news.title.includes(keyword))
        ||  
        (news.time && news.time.includes(keyword))
        ||
        (news.abstract && news.abstract.includes(keyword))
        ||
        (news.content && news.content.includes(keyword))
        ||
        (news.source && news.source.includes(keyword))
        ||
        (news.topic && news.topic.includes(keyword))
);

    });

    let html="";
    result.forEach(news=>{
        html +=
        `
        <div class="news-card">
        <div class="news-source"> ${highlight(news.source, keyword)} </div>
        <div class="news-title"> ${highlight(news.title, keyword)} </div>
        <div class="news-time"> ${highlight(news.time, keyword)} </div>
        <div class="news-abstract"> ${highlight(news.abstract, keyword)} </div>
        <a class="detail-btn" href="detail/${news.id}.html">查看详情</a>
        </div>
        `;
    });
    document.getElementById("news-list").innerHTML = html;
    document.getElementById("search-keyword").innerHTML = keyword;
}

let newsList = [];
fetch("news.json")
.then(response => response.json())
.then(data => {
    newsList = data;
    let button = document.getElementById("search-button");
    button.addEventListener("click", function(){
        search(newsList);
    });
    search(newsList);
})
